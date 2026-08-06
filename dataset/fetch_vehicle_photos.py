"""Download ~50 car front-view photos from Wikimedia Commons (no API key needed)."""
import json
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

OUT_DIR = Path(__file__).parent / "vehicle_source"
OUT_DIR.mkdir(exist_ok=True)
TARGET = 50
THUMB_WIDTH = 320

QUERIES = [
    "Hyundai front view",
    "Kia front view",
    "Genesis car front view",
    "Hyundai Sonata front",
    "Hyundai Avante front",
    "Hyundai Elantra front",
    "Kia K5 front",
    "Kia Sorento front",
    "Hyundai Tucson front",
    "Hyundai Santa Fe front",
    "Kia Sportage front",
    "Kia Carnival front",
    "Hyundai Ioniq front",
    "Kia EV6 front",
    "Genesis G80 front",
    "Hyundai Palisade front",
    "Kia Seltos front",
    "Hyundai Grandeur front",
]

API = "https://commons.wikimedia.org/w/api.php"
HEADERS = {"User-Agent": "SideEyeDatasetBot/1.0 (educational project)"}


def api_get(params):
    url = API + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers=HEADERS)
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            if e.code == 429:
                time.sleep(2 * (attempt + 1))
                continue
            raise
    raise RuntimeError("too many retries")


def fetch_bytes(url):
    req = urllib.request.Request(url, headers=HEADERS)
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code == 429:
                time.sleep(2 * (attempt + 1))
                continue
            raise
    raise RuntimeError("too many retries")


def search_titles(query, limit=20):
    data = api_get({
        "action": "query", "list": "search", "srsearch": query,
        "srnamespace": 6, "srlimit": limit, "format": "json",
    })
    return [r["title"] for r in data["query"]["search"]]


def thumb_url(title):
    data = api_get({
        "action": "query", "titles": title, "prop": "imageinfo",
        "iiprop": "url|mime", "iiurlwidth": THUMB_WIDTH, "format": "json",
    })
    pages = data["query"]["pages"]
    for p in pages.values():
        info = p.get("imageinfo")
        if not info:
            return None
        mime = info[0].get("mime", "")
        if not mime.startswith("image/jpeg") and not mime.startswith("image/png"):
            return None
        return info[0].get("thumburl") or info[0].get("url")
    return None


def main():
    seen_titles = set()
    downloaded = len(list(OUT_DIR.glob("vehicle_*.jpg")))
    print(f"resuming from {downloaded} existing images")
    for q in QUERIES:
        if downloaded >= TARGET:
            break
        try:
            titles = search_titles(q)
        except Exception as e:
            print(f"search failed for {q!r}: {e}")
            continue
        time.sleep(1.0)
        for title in titles:
            if downloaded >= TARGET:
                break
            if title in seen_titles or not title.lower().endswith((".jpg", ".jpeg", ".png")):
                continue
            seen_titles.add(title)
            try:
                url = thumb_url(title)
                if not url:
                    continue
                fname = OUT_DIR / f"vehicle_{downloaded+1:03d}.jpg"
                data = fetch_bytes(url)
                with open(fname, "wb") as f:
                    f.write(data)
                downloaded += 1
                print(f"[{downloaded}/{TARGET}] {title}")
            except Exception as e:
                print(f"skip {title!r}: {e}")
            time.sleep(1.0)

    print(f"Done: {downloaded} images in {OUT_DIR}")


if __name__ == "__main__":
    main()
