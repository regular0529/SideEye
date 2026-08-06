"""List actual files left in vehicle_source/ and background_source/ (after
manual cleanup) into dataset/manifest.json, so auto_collect.html doesn't
assume contiguous numbering."""
import json
from pathlib import Path

BASE = Path(__file__).parent
manifest = {
    "vehicle": sorted(p.name for p in (BASE / "vehicle_source").glob("*.jpg")),
    "background": sorted(p.name for p in (BASE / "background_source").glob("*.jpg")),
}
(BASE / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
print(f"vehicle: {len(manifest['vehicle'])}, background: {len(manifest['background'])}")
