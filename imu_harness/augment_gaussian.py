"""
SideEye IMU dataset augmentation via Gaussian jitter.

Takes the real (non-augmented) recordings for a label already in the Edge
Impulse project, adds per-channel Gaussian noise scaled to that channel's
own observed variability, and uploads synthetic copies until the label
reaches a target count. Real recordings are the only source of true
information -- this multiplies them for training volume/regularization,
it does not invent new motion patterns.

Usage:
    python augment_gaussian.py <label> [target_count] [noise_factor]

Example:
    python augment_gaussian.py left 500 0.15
"""
import json
import math
import os
import random
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

from dotenv import load_dotenv

PROJECT_ID = "1079933"
INTERVAL_MS = 20


def fetch_real_samples(key: str, label: str) -> list:
    req = urllib.request.Request(
        f"https://studio.edgeimpulse.com/v1/api/{PROJECT_ID}/raw-data?category=training",
        headers={"x-api-key": key},
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        samples = json.load(r).get("samples", [])
    real_ids = [
        s["id"] for s in samples
        if s.get("label") == label and "gaussianaug" not in (s.get("filename") or "")
    ]

    values_list = []
    for sid in real_ids:
        req2 = urllib.request.Request(
            f"https://studio.edgeimpulse.com/v1/api/{PROJECT_ID}/raw-data/{sid}",
            headers={"x-api-key": key},
        )
        with urllib.request.urlopen(req2, timeout=30) as r2:
            values_list.append(json.load(r2)["payload"]["values"])
    return values_list


def channel_stds(values_list: list) -> list:
    n_channels = len(values_list[0][0])
    stds = []
    for ch in range(n_channels):
        all_vals = [row[ch] for values in values_list for row in values]
        mean = sum(all_vals) / len(all_vals)
        variance = sum((v - mean) ** 2 for v in all_vals) / len(all_vals)
        stds.append(math.sqrt(variance) or 0.1)  # avoid zero-noise on a flat channel
    return stds


def jitter(values: list, stds: list, noise_factor: float) -> list:
    return [
        [v + random.gauss(0, std * noise_factor) for v, std in zip(row, stds)]
        for row in values
    ]


def upload_sample(key: str, label: str, values: list) -> None:
    payload = {
        "protected": {"ver": "v1", "alg": "none", "iat": int(time.time())},
        "signature": "0",
        "payload": {
            "device_name": "sideeye-master-xiao-esp32s3-gaussianaug",
            "device_type": "XIAO_ESP32S3",
            "interval_ms": INTERVAL_MS,
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
    req = urllib.request.Request(
        "https://ingestion.edgeimpulse.com/api/training/data",
        data=body,
        method="POST",
        headers={
            "x-api-key": key,
            "x-label": label,
            "x-file-name": f"{label}.gaussianaug.{int(time.time() * 1000)}.{random.randint(0,999999)}.json",
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as response:
        if response.status not in (200, 201, 202):
            raise RuntimeError(f"HTTP {response.status}")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    label = sys.argv[1]
    target_count = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    noise_factor = float(sys.argv[3]) if len(sys.argv) > 3 else 0.15

    load_dotenv()
    key = os.getenv("SideEYE_EI_API_KEY")
    if not key:
        print("SideEYE_EI_API_KEY missing from .env")
        return 1

    real = fetch_real_samples(key, label)
    if not real:
        print(f"No real (non-augmented) samples found for label '{label}'")
        return 1
    print(f"Found {len(real)} real samples for '{label}'")

    stds = channel_stds(real)
    print(f"Per-channel std (noise base): {[round(s, 3) for s in stds]}")

    to_generate = max(0, target_count - len(real))
    print(f"Generating {to_generate} augmented samples (noise_factor={noise_factor}, 5 workers in parallel)")

    def make_and_upload(i):
        base = real[i % len(real)]
        synthetic = jitter(base, stds, noise_factor)
        upload_sample(key, label, synthetic)

    uploaded = 0
    failed = 0
    with ThreadPoolExecutor(max_workers=5) as pool:
        futures = {pool.submit(make_and_upload, i): i for i in range(to_generate)}
        for future in as_completed(futures):
            try:
                future.result()
                uploaded += 1
            except Exception as e:
                failed += 1
                print(f"  upload failed at {futures[future]}: {e}")
            if (uploaded + failed) % 50 == 0 or (uploaded + failed) == to_generate:
                print(f"  {uploaded + failed}/{to_generate} done ({uploaded} ok, {failed} failed)")

    print(f"Done. {len(real)} real + {uploaded} augmented = {len(real) + uploaded} total for '{label}'")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
