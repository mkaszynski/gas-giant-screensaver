#!/usr/bin/env python3
"""Fetch the pinned numerical inputs for offline galaxy synthesis.

The Edenhofer subset contains the original primary/MEAN HDUs and the original
radial/inner-column metadata. Unused STD data is not downloaded or fabricated.
All downloaded bytes are checked against pinned SHA-256 digests before rename.
"""

import argparse
import concurrent.futures
import hashlib
import time
import urllib.request
from pathlib import Path
from render_galaxy import prepare_dust

CSFD_URL = "https://zenodo.org/records/8207175/files/csfd_ebv.fits?download=1"
EDENHOFER_URL = (
    "https://zenodo.org/records/8187943/files/mean_and_std_healpix.fits?download=1"
)
INPUTS = {
    "csfd_ebv.fits": (
        "379017a424b6f50e98d03df1eac7417750dc0ceba31a34441c63ea182ce01f6e",
        201335040,
    ),
    "edenhofer-mean.fits": (
        "4bad79d417107c1a090779bb60d45b1fd4a2f5716797f34b58b5180f69cbf984",
        1623202560,
    ),
    "dust-metadata.bin": (
        "397bcb45b72d444ec20ac33e5f1f3d6c12162d512add1a5a5251890be9d43cac",
        6312960,
    ),
}


def valid(path):
    if not path.is_file() or path.stat().st_size != INPUTS[path.name][1]:
        return False
    h = hashlib.sha256()
    with path.open("rb") as f:
        while b := f.read(1024 * 1024):
            h.update(b)
    return h.hexdigest() == INPUTS[path.name][0]


def fetch_part(url, path, source_offset, length, destination_offset=0):
    for attempt in range(4):
        try:
            req = urllib.request.Request(
                url + f"&galaxy-range={source_offset}",
                headers={"Range": f"bytes={source_offset}-{source_offset+length-1}"},
            )
            with urllib.request.urlopen(req, timeout=60) as src, path.open(
                "r+b"
            ) as out:
                if (
                    src.status != 206
                    or src.headers.get("Content-Range")
                    != f"bytes {source_offset}-{source_offset+length-1}/3252715200"
                ):
                    raise RuntimeError(
                        "Server did not return the requested dust-data range"
                    )
                out.seek(destination_offset)
                left = length
                while left:
                    b = src.read(min(left, 1024 * 1024))
                    if not b:
                        raise RuntimeError("Truncated dust-data range")
                    out.write(b)
                    left -= len(b)
            return
        except Exception:
            if attempt == 3:
                raise
            time.sleep(2)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--workers", type=int, choices=range(1, 9), default=4)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    for name, (_, size) in INPUTS.items():
        target = a.output / name
        if valid(target):
            print("Verified cached", name, flush=True)
            continue
        temporary = target.with_suffix(".download")
        with temporary.open("wb") as f:
            f.truncate(size)
        if name == "csfd_ebv.fits":
            with urllib.request.urlopen(CSFD_URL, timeout=60) as src, temporary.open(
                "wb"
            ) as out:
                left = size
                while left:
                    b = src.read(min(left, 1024 * 1024))
                    if not b:
                        raise RuntimeError("Truncated CSFD input")
                    out.write(b)
                    left -= len(b)
                if src.read(1):
                    raise RuntimeError("Unexpected CSFD length")
        elif name == "dust-metadata.bin":
            fetch_part(EDENHOFER_URL, temporary, 3246402240, size)
        else:
            chunk = 32 * 1024 * 1024
            with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
                tasks = [
                    pool.submit(
                        fetch_part, EDENHOFER_URL, temporary, i, min(chunk, size - i), i
                    )
                    for i in range(0, size, chunk)
                ]
                for i, t in enumerate(concurrent.futures.as_completed(tasks)):
                    t.result()
                    print("Mean subset", i + 1, "/", len(tasks), "ranges", flush=True)
        # Verify using the intended target name while keeping the target atomic.
        h = hashlib.sha256()
        with temporary.open("rb") as f:
            while b := f.read(1024 * 1024):
                h.update(b)
        if temporary.stat().st_size != size or h.hexdigest() != INPUTS[name][0]:
            raise RuntimeError("Dust input checksum mismatch: " + name)
        temporary.replace(target)
        print("Fetched and verified", name, flush=True)
    prepare_dust(
        a.output / "edenhofer-mean.fits",
        a.output / "dust-metadata.bin",
        a.output / "dust-profiles.npz",
    )


if __name__ == "__main__":
    main()
