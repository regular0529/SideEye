"""
SideEye IMU web collector -> Edge Impulse upload bridge.

Mirrors edge_impulse_upload_proxy.py's role for the vision collector: the
browser (talking to the ESP32's AP) never sees the API key. This local
server holds SideEYE_EI_API_KEY (from .env) and forwards each recorded
window to the Edge Impulse ingestion API.

Usage:
    python imu_upload_bridge.py
    (keep this window open while using the browser collector)

The ESP32 AP and this bridge are on DIFFERENT networks (the ESP32's AP has
no internet), so use the same two-step flow as the vision guide: connect to
"sideeye-imu" while recording, then switch back to internet WiFi before
pressing "업로드" in the browser page.
"""
import json
import os
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib import request as urlrequest
from urllib.error import HTTPError

from dotenv import load_dotenv

load_dotenv()
API_KEY = os.getenv("SideEYE_EI_API_KEY")
SAMPLE_INTERVAL_MS = 20  # must match imu_web_collector.ino


def upload_to_edge_impulse(label: str, values: list) -> None:
    payload = {
        "protected": {"ver": "v1", "alg": "none", "iat": int(time.time())},
        "signature": "0",
        "payload": {
            "device_name": "sideeye-master-xiao-esp32s3-web",
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
    req = urlrequest.Request(
        "https://ingestion.edgeimpulse.com/api/training/data",
        data=body,
        method="POST",
        headers={
            "x-api-key": API_KEY,
            "x-label": label,
            "x-file-name": f"{label}.web.{int(time.time() * 1000)}.json",
            "Content-Type": "application/json",
        },
    )
    with urlrequest.urlopen(req, timeout=30) as response:
        if response.status not in (200, 201, 202):
            raise RuntimeError(f"HTTP {response.status}")


class Handler(BaseHTTPRequestHandler):
    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self._cors()
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"ok": bool(API_KEY)}).encode())
            return
        self.send_response(404)
        self._cors()
        self.end_headers()

    def do_POST(self):
        if self.path != "/upload":
            self.send_response(404)
            self._cors()
            self.end_headers()
            return

        length = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(length))
        label = body.get("label")
        values = body.get("values")

        try:
            upload_to_edge_impulse(label, values)
            self.send_response(200)
            self._cors()
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"success": True}).encode())
            print(f"uploaded: {label} ({len(values)} rows)")
        except Exception as error:
            self.send_response(502)
            self._cors()
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"success": False, "error": str(error)}).encode())
            print(f"upload failed: {label}: {error}")

    def log_message(self, format, *args):
        pass  # quiet; we print our own upload lines above


def main():
    if not API_KEY:
        print("SideEYE_EI_API_KEY is missing from .env")
        return 1

    server = HTTPServer(("127.0.0.1", 8787), Handler)
    print("SideEye IMU upload bridge: http://127.0.0.1:8787")
    print("API key loaded from .env (value hidden)")
    print("Keep this window open. Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
