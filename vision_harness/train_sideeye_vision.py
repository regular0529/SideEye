"""Upload new vehicle/background photos and retrain the SideEYEVision Edge
Impulse project (1080527) end to end: upload -> rebalance -> generate
features -> train -> build Arduino library -> download.

Usage:
    python vision_harness/train_sideeye_vision.py --data-dir dataset/new_capture

`data-dir` must contain `vehicle/` and `background/` subfolders of JPEGs
(however many are left after manual cleanup — counts are not assumed).
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from dotenv import load_dotenv
import os

sys.path.insert(0, str(Path(__file__).parent))
from upload_edge_impulse import multipart  # noqa: E402

PROJECT_ID = 1080527
DSP_ID = 2
LEARN_ID = 3
API = f"https://studio.edgeimpulse.com/v1/api/{PROJECT_ID}"


def api_request(key, method, path, body=None):
    url = f"{API}{path}"
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "x-api-key": key,
        "Content-Type": "application/json",
    })
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


def poll_job(key, job_id, label):
    print(f"{label}: job {job_id} started, polling...")
    while True:
        status = api_request(key, "GET", f"/jobs/{job_id}/status")
        job = status.get("job", {})
        if job.get("finished"):
            ok = job.get("finishedSuccessful")
            print(f"{label}: finished, success={ok}")
            if not ok:
                logs = api_request(key, "GET", f"/jobs/{job_id}/stdout")
                for line in logs.get("stdout", [])[:40]:
                    print("  ", line.get("data", ""))
                raise RuntimeError(f"{label} failed")
            return
        time.sleep(5)


def upload_folder(key, folder: Path, label: str):
    files = sorted(folder.glob("*.jpg"))
    if not files:
        print(f"no jpg files in {folder}, skipping upload for {label}")
        return 0
    for i, f in enumerate(files, 1):
        body, boundary = multipart(f)
        req = urllib.request.Request(
            "https://ingestion.edgeimpulse.com/api/training/files",
            data=body, method="POST",
            headers={
                "x-api-key": key,
                "x-label": label,
                "Content-Type": f"multipart/form-data; boundary={boundary}",
            },
        )
        with urllib.request.urlopen(req, timeout=60) as r:
            if r.status not in (200, 201, 202):
                raise RuntimeError(f"upload failed for {f.name}: HTTP {r.status}")
        if i == 1 or i % 10 == 0 or i == len(files):
            print(f"  uploaded {label} {i}/{len(files)}")
    return len(files)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vehicle-dir", default="dataset/new_capture/vehicle")
    parser.add_argument("--background-dir", default="dataset/new_capture/background")
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--training-cycles", type=int, default=20)
    args = parser.parse_args()

    load_dotenv()
    key = os.getenv("SideEYEVision_EI_API_KEY")
    if not key:
        print("SideEYEVision_EI_API_KEY missing from .env")
        return 1

    if not args.skip_upload:
        n_v = upload_folder(key, Path(args.vehicle_dir), "vehicle")
        n_b = upload_folder(key, Path(args.background_dir), "background")
        print(f"uploaded: vehicle={n_v}, background={n_b}")
        api_request(key, "POST", "/rebalance")
        print("rebalanced train/test split")

    gen = api_request(key, "POST", "/jobs/generate-features", {"dspId": DSP_ID, "calculateFeatureImportance": False})
    poll_job(key, gen["id"], "generate-features")

    train = api_request(key, "POST", f"/jobs/train/keras/{LEARN_ID}",
                         {"trainingCycles": args.training_cycles, "learningRate": 0.0005})
    poll_job(key, train["id"], "train")

    stdout = api_request(key, "GET", f"/jobs/{train['id']}/stdout")
    acc_lines = [l["data"] for l in stdout.get("stdout", []) if "val_accuracy" in l.get("data", "")]
    if acc_lines:
        print("accuracy log (last 5):")
        for line in acc_lines[-5:]:
            print("  ", line)
    else:
        print("no val_accuracy lines found in train stdout")

    build = api_request(key, "POST", "/jobs/build-ondevice-model?type=arduino", {"engine": "tflite-eon"})
    poll_job(key, build["id"], "build-arduino")

    zip_url = f"{API}/deployment/download?type=arduino"
    req = urllib.request.Request(zip_url, headers={"x-api-key": key})
    out_zip = Path("vision_harness/sideeye_vision_arduino.zip")
    with urllib.request.urlopen(req, timeout=120) as r:
        out_zip.write_bytes(r.read())
    print(f"Arduino library saved to {out_zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
