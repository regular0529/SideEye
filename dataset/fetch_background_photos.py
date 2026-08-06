"""Download ~50 car-free background photos from Wikimedia Commons (no API key needed).
Mostly empty road/parking scenes (hardest negatives for a vehicle detector),
plus general backgrounds for variety."""
import json
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

OUT_DIR = Path(__file__).parent / "background_source"
OUT_DIR.mkdir(exist_ok=True)
TARGET = 50
THUMB_WIDTH = 320

QUERIES = [
    # empty road / parking - hardest negatives, prioritized first
    "empty road no cars",
    "empty street",
    "empty parking lot",
    "empty highway",
    "empty alley",
    "empty crosswalk",
    "rural road empty",
    "residential street empty",
    "empty driveway",
    "sidewalk empty",
    # general backgrounds for variety
    "office desk",
    "hallway interior",
    "blank wall texture",
    "sky clouds",
    "tree forest path",
    "living room interior",
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
    downloaded = len(list(OUT_DIR.glob("background_*.jpg")))
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
                fname = OUT_DIR / f"background_{downloaded+1:03d}.jpg"
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
