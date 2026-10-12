#!/usr/bin/env python3
"""Fetch published 2m Swiss elevation tiles and a pinned distant-relief tile.

The first discovery writes a source manifest. Subsequent downloads use its
immutable URLs and SHA256 checksums. No color photography is downloaded.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import time
import urllib.request

API = "https://data.geo.admin.ch/api/stac/v1/collections/ch.swisstopo.swissalti3d/items?bbox=7.695,46.475,7.81,46.535&limit=100"


def request(url):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=90) as r:
                return r.read()
        except Exception:
            if attempt == 3:
                raise
            time.sleep(2**attempt)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.manifest.exists():
        sources = json.loads(args.manifest.read_text())["sources"]
    else:
        newest = {}
        url = API
        while url:
            page = json.loads(request(url))
            for f in page["features"]:
                tile = f["id"].split("_")[-1]
                for key, asset in f["assets"].items():
                    if key.endswith("_2_2056_5728.tif") and (
                        tile not in newest or f["id"] > newest[tile]["id"]
                    ):
                        newest[tile] = {
                            "id": f["id"],
                            "url": asset["href"],
                            "file": key,
                        }
            url = next((l["href"] for l in page["links"] if l["rel"] == "next"), None)
        sources = list(newest.values())

    def fetch(s):
        file = args.output / s["file"]
        data = file.read_bytes() if file.exists() else request(s["url"])
        sha = hashlib.sha256(data).hexdigest()
        if "sha256" in s and s["sha256"] != sha:
            raise ValueError("Source checksum mismatch: " + s["file"])
        temp = file.with_suffix(file.suffix + ".tmp")
        temp.write_bytes(data)
        temp.replace(file)
        return dict(s, sha256=sha)

    with ThreadPoolExecutor(max_workers=4) as pool:
        sources = list(pool.map(fetch, sources))
    args.manifest.write_text(
        json.dumps(
            {
                "attribution": "Federal Office of Topography swisstopo",
                "terms": "https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices",
                "sources": sources,
            },
            indent=2,
        )
        + "\n"
    )
    print(f"Fetched and verified {len(sources)} published 2m tiles", flush=True)


if __name__ == "__main__":
    main()
