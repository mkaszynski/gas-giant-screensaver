#!/usr/bin/env python3
"""Bake measured HYG stars once; the runtime never generates star coordinates."""

import argparse
import csv
import hashlib
import json
import math
import struct
from pathlib import Path
import numpy as np

OBLIQUITY = math.radians(23.4392911)


def cie_fit(w):
    """Wyman/Sloan/Shirley 2013 analytic approximation of CIE 1931 2-deg CMFs."""

    def g(center, left, right):
        t = (w - center) * np.where(w < center, left, right)
        return np.exp(-0.5 * t * t)

    return np.stack(
        (
            1.056 * g(599.8, 0.0264, 0.0323)
            + 0.362 * g(442.0, 0.0624, 0.0374)
            - 0.065 * g(501.1, 0.049, 0.0382),
            0.821 * g(568.8, 0.0213, 0.0247) + 0.286 * g(530.9, 0.0613, 0.0322),
            1.217 * g(437.0, 0.0845, 0.0278) + 0.681 * g(459.0, 0.0385, 0.0725),
        ),
        axis=1,
    )


def stellar_color(bv):
    # Ballesteros B-V -> effective blackbody temperature; clamp outside fit range.
    bv = np.clip(bv, -0.4, 2.0)
    temperature = 4600 * (1 / (0.92 * bv + 1.7) + 1 / (0.92 * bv + 0.62))
    wavelength = np.arange(380.0, 781.0, 2.0)
    spectrum = wavelength**-5 / np.expm1(1.438776877e7 / (wavelength * temperature))
    xyz = spectrum @ cie_fit(wavelength)
    rgb = (
        np.array(
            [
                [3.2406, -1.5372, -0.4986],
                [-0.9689, 1.8758, 0.0415],
                [0.0557, -0.2040, 1.0570],
            ]
        )
        @ xyz
    )
    rgb = np.maximum(rgb, 0.0)
    return rgb / (rgb @ [0.2126, 0.7152, 0.0722])


def bake_catalog(path, out):
    records = []
    missing = 0
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            if row["id"] == "0" or float(row["mag"]) > 6.5:
                continue
            ra, dec = float(row["ra"]) * math.pi / 12, math.radians(float(row["dec"]))
            x, y, z = (
                math.cos(dec) * math.cos(ra),
                math.sin(dec),
                math.cos(dec) * math.sin(ra),
            )
            direction = (
                x,
                y * math.cos(OBLIQUITY) - z * math.sin(OBLIQUITY),
                y * math.sin(OBLIQUITY) + z * math.cos(OBLIQUITY),
            )
            mag = float(row["mag"])
            if row["ci"]:
                color = stellar_color(float(row["ci"]))
            else:
                color = np.ones(3)  # Missing photometry stays neutral, never random.
                missing += 1
            records.append((*direction, 10 ** (-0.4 * mag), *color, mag))
    with (out / "stars.bin").open("wb") as f:
        f.write(b"GGSSTAR1" + struct.pack("<I", len(records)))
        for record in records:
            f.write(struct.pack("<8f", *record))
    return {
        "stars": len(records),
        "missing_color_indices": missing,
        "limiting_magnitude": 6.5,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--catalog", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    expected = "d9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd"
    if hashlib.sha256(a.catalog.read_bytes()).hexdigest() != expected:
        p.error("Use the pinned HYG v4.1 catalog")
    a.output.mkdir(parents=True, exist_ok=True)
    metadata = bake_catalog(a.catalog, a.output)
    metadata["sources"] = {a.catalog.name: expected}
    (a.output / "catalog-manifest.json").write_text(
        json.dumps(metadata, indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
