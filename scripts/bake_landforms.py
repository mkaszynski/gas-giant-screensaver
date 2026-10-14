#!/usr/bin/env python3
"""Cache coherent measured arid and coastal relief for the fictional moon."""

import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates, gaussian_filter
from bake_world import periodic_terrain, relief_material

SOURCES = {
    "arid": {
        "tile": "N36W113",
        "sha256": "610e98d96ec4a1627f25b49f99cc61200527ad0314e0be4221d9b30f3276ac06",
        "tile_lat": 36,
        "tile_lon": -113,
        "latitude": 36.205,
        "longitude": -112.275,
        "description": "Connected Colorado Plateau canyon drainage and cliff forms",
        "height_scale": 0.65,
    },
    "coastal": {
        "tile": "N48W122",
        "sha256": "1d86041e91c1423b1bc05031b9f67d31a809f2ed742e2409351e7bcb71859ec6",
        "tile_lat": 48,
        "tile_lon": -122,
        "latitude": 48.15,
        "longitude": -121.73,
        "description": "Pacific Northwest valley and foothill structure",
        "height_scale": 0.38,
    },
}


def extract(dem, source, size=2048, spacing=16):
    if dem.shape != (3601, 3601) or not np.isfinite(dem).all() or (dem == -32768).any():
        raise ValueError("invalid or void-containing DEM")
    a = (np.arange(size) + 0.5 - size * 0.5) * spacing
    lat = source["latitude"] + a[:, None] / (np.pi * 6371000 / 180)
    lon = source["longitude"] + a[None, :] / (
        np.pi * 6371000 / 180 * np.cos(np.deg2rad(source["latitude"]))
    )
    rows = np.broadcast_to((source["tile_lat"] + 1 - lat) * 3600, (size, size))
    cols = np.broadcast_to((lon - source["tile_lon"]) * 3600, (size, size))
    if rows.min() < 0 or cols.min() < 0 or rows.max() > 3600 or cols.max() > 3600:
        raise ValueError("landform window crosses source boundary")
    sampled = map_coordinates(dem.astype(float), (rows, cols), order=1, mode="nearest")
    coherent = gaussian_filter(periodic_terrain(sampled), 0.6, mode="wrap")
    return np.round(coherent * source["height_scale"]).astype("<i2")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sources", type=Path, required=True)
    p.add_argument("--output", type=Path, default=Path("assets/world/landforms"))
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {
        "version": 1,
        "size": 2048,
        "spacing_m": 16,
        "span_m": 32768,
        "processing": "Measured connected relief; periodic-plus-smooth boundary correction; relative heights",
        "photographic_input": False,
        "normal_field": "Full measured height gradient; continental and canopy normals separate",
        "sources": {},
    }
    for name, source in SOURCES.items():
        file = args.sources / (source["tile"] + ".hgt.gz")
        if hashlib.sha256(file.read_bytes()).hexdigest() != source["sha256"]:
            raise ValueError("source checksum mismatch: " + str(file))
        dem = np.frombuffer(gzip.decompress(file.read_bytes()), ">i2").reshape(
            3601, 3601
        )
        h = extract(dem, source)
        path = args.output / (name + ".ggrelief")
        path.write_bytes(
            struct.pack("<8sIIf", b"GGRELIF1", 2048, 2048, 16) + h.tobytes()
        )
        material = args.output / (name + "-material.png")
        Image.fromarray(relief_material(h, 16)).save(material)
        manifest["sources"][name] = {
            **source,
            "url": "https://elevation-tiles-prod.s3.amazonaws.com/skadi/"
            + source["tile"][:3]
            + "/"
            + source["tile"]
            + ".hgt.gz",
            "relief_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "material_sha256": hashlib.sha256(material.read_bytes()).hexdigest(),
            "range_m": [int(h.min()), int(h.max())],
            "rms_m": float(h.std()),
        }
        print(
            name,
            manifest["sources"][name]["range_m"],
            manifest["sources"][name]["rms_m"],
            flush=True,
        )
    manifest_file = args.output / "manifest.json"
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n")
    world_file = args.output.parent / "manifest.json"
    if world_file.exists():
        world = json.loads(world_file.read_text())
        world["landforms_manifest_sha256"] = hashlib.sha256(
            manifest_file.read_bytes()
        ).hexdigest()
        world_file.write_text(json.dumps(world, indent=2) + "\n")


if __name__ == "__main__":
    main()
