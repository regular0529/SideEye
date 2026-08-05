"""
SideEye IMU data collector -> Edge Impulse uploader.

Reads CSV samples streamed by firmware/master/imu_collector/imu_collector.ino
over serial (millis,ax,ay,az,gx,gy,gz at ~50Hz) and uploads fixed-duration
windows to Edge Impulse as labeled time-series samples, using the same
ingestion-API-over-urllib pattern as vision_harness/upload_edge_impulse.py
(no edge-impulse-cli needed).

Usage:
    python collect_and_upload_imu.py <label> <port> [count] [window_seconds]

Example (idle/left-lean/right-lean, 50 samples each, 2s windows):
    python collect_and_upload_imu.py idle COM13 50 2.0
    python collect_and_upload_imu.py left COM13 50 2.0
    python collect_and_upload_imu.py right COM13 50 2.0

Mounting reminder (PDR_SideEye.md section 3.1): BNO055 must be mounted in
its final helmet position (pin header against the back of the head) during
collection -- a model trained in a different orientation will not transfer.
"""

import json
import os
import sys
import time
import urllib.request
import winsound
from pathlib import Path

import serial
from dotenv import load_dotenv

PROJECT_ID = "1079933"
SAMPLE_INTERVAL_MS = 20  # must match SAMPLE_INTERVAL_MS in imu_collector.ino


def wait_for_ready(port: serial.Serial) -> None:
    # The firmware streams continuously once running; a one-shot "READY" line
    # is easy to miss if the port was already open/streaming before this
    # script attached (opening the port does not reliably reset the board
    # the way arduino-cli's upload sequence does). Just drain a warm-up
    # window instead of waiting for a specific line.
    port.reset_input_buffer()
    warmup_deadline = time.time() + 1.5
    while time.time() < warmup_deadline:
        port.readline()


def capture_window(port: serial.Serial, window_seconds: float) -> list[list[float]]:
    port.reset_input_buffer()
    values: list[list[float]] = []
    deadline = time.time() + window_seconds
    while time.time() < deadline:
        raw = port.readline().decode(errors="ignore").strip()
        if not raw or raw == "READY":
            continue
        parts = raw.split(",")
        if len(parts) != 7:
            continue
        try:
            # drop millis (parts[0]); keep ax,ay,az,gx,gy,gz
            values.append([float(x) for x in parts[1:]])
        except ValueError:
            continue
    return values


def upload_sample(api_key: str, label: str, values: list[list[float]]) -> None:
    payload = {
        "protected": {"ver": "v1", "alg": "none", "iat": int(time.time())},
        "signature": "0",
        "payload": {
            "device_name": "sideeye-master-xiao-esp32s3",
            "device_type": "XIAO_ESP32S3",
            "interval_ms": SAMPLE_INTERVAL_MS,
            "sensors": [
                {"name": "accX", "units": "m/s2"},
                {"name": "accY", "units": "m/s2"},
                {"name": "accZ", "units": "m/s2"},
                {"name": "gyrX", "units": "deg/s"},
                {"name": "gyrY", "units": "deg/s"},
                {"name": "gyrZ", "units": "deg/s"},
            ],
            "values": values,
        },
    }
    body = json.dumps(payload).encode()
    request = urllib.request.Request(
        "https://ingestion.edgeimpulse.com/api/training/data",
        data=body,
        method="POST",
        headers={
            "x-api-key": api_key,
            "x-label": label,
            "x-file-name": f"{label}.{int(time.time() * 1000)}.json",
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            if response.status not in (200, 201, 202):
                raise RuntimeError(f"HTTP {response.status}")
    except urllib.error.HTTPError as error:
        detail = error.read().decode(errors="ignore")
        raise RuntimeError(f"HTTP {error.code}: {detail}") from None


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    label = sys.argv[1]
    port_name = sys.argv[2]
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 50
    window_seconds = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0

    load_dotenv()
    api_key = os.getenv("SideEYE_EI_API_KEY")
    if not api_key:
        print("SideEYE_EI_API_KEY is missing from .env")
        return 1

    print(f"Project: https://studio.edgeimpulse.com/studio/{PROJECT_ID}")
    print(f"Label: {label} | Port: {port_name} | Samples: {count} | Window: {window_seconds}s")
    print("Reminder: BNO055 must be mounted in its final helmet position (back of head).")

    port = serial.Serial(port_name, 115200, timeout=1)
    wait_for_ready(port)
    print("Board ready.")

    uploaded = 0
    for i in range(1, count + 1):
        print(f"\nSample {i}/{count} [{label}] -- get ready")
        winsound.Beep(500, 100)
        time.sleep(1.5)
        for n in (3, 2, 1):
            print(n)
            winsound.Beep(600, 150)
            time.sleep(0.85)
        print(f"GO -- perform the motion now ({window_seconds:.0f}s)")
        winsound.Beep(1200, 250)
        values = capture_window(port, window_seconds)
        winsound.Beep(400, 250)
        print("Stop.")
        if len(values) < 10:
            print(f"  Skipped: only {len(values)} rows captured, check wiring/serial")
            continue

        # Sanity check: show the actual data range so you can SEE whether the
        # motion registered, not just trust that rows came in.
        accel_rows = [row[:3] for row in values]
        accel_mag = [sum(v * v for v in row) ** 0.5 for row in accel_rows]
        print(f"  {len(values)} rows | accel magnitude min={min(accel_mag):.2f} max={max(accel_mag):.2f} m/s^2"
              f" | first row: {values[0]}")

        # Hard guard: a stalled I2C bus (e.g. a jumper wire flexing loose
        # while the wearer moves) makes the BNO055 driver return all-zero
        # rows silently instead of erroring. Never upload that as real data.
        zero_rows = sum(1 for row in values if all(v == 0 for v in row))
        if zero_rows / len(values) > 0.3:
            print(f"  SKIPPED: {zero_rows}/{len(values)} rows are all-zero -- I2C likely dropped out, not uploading")
            continue
        if label != "idle" and max(accel_mag) < 1.0:
            print("  WARNING: peak accel looks flat for a non-idle label -- motion may not have registered")

        try:
            upload_sample(api_key, label, values)
            uploaded += 1
            print(f"  Uploaded ({len(values)} rows). Total uploaded: {uploaded}/{count}")
        except Exception as error:
            print(f"  Upload failed: {error}")

    port.close()
    print(f"\nDone. {uploaded}/{count} samples uploaded for label '{label}'.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
