#!/usr/bin/env python3
"""Bake a seamless fictional Earth-size moon. No photographic input/runtime GIS."""

import argparse, hashlib, json, struct
from pathlib import Path
import numpy as np
from scipy.ndimage import gaussian_filter
from PIL import Image


def noise3(x, y, z, scale, seed):
    x, y, z = x * scale, y * scale, z * scale
    ix, iy, iz = (
        np.floor(x).astype(np.int64),
        np.floor(y).astype(np.int64),
        np.floor(z).astype(np.int64),
    )
    fx, fy, fz = x - ix, y - iy, z - iz
    fx, fy, fz = [f * f * (3 - 2 * f) for f in (fx, fy, fz)]

    def h(a, b, c):
        n = (a * 73856093 ^ b * 19349663 ^ c * 83492791 ^ seed) & 0xFFFFFFFF
        n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
        return (n ^ (n >> 16)) / 4294967295.0

    out = np.zeros_like(x)
    for a in (0, 1):
        for b in (0, 1):
            for c in (0, 1):
                out += (
                    h(ix + a, iy + b, iz + c)
                    * (fx if a else 1 - fx)
                    * (fy if b else 1 - fy)
                    * (fz if c else 1 - fz)
                )
    return out


def build(width=2048, height=1024):
    lon = (np.arange(width) + 0.5) * 2 * np.pi / width - np.pi
    lat = (np.arange(height) + 0.5) * np.pi / height - np.pi / 2
    x = np.cos(lat)[:, None] * np.cos(lon)[None, :]
    y = np.broadcast_to(np.sin(lat)[:, None], x.shape)
    z = np.cos(lat)[:, None] * np.sin(lon)[None, :]
    # Coherent continents and mountain belts, continuous at longitude seams.
    continental = (
        noise3(x, y, z, 3.7, 734) * 0.65
        + noise3(x, y, z, 8.1, 115) * 0.25
        + noise3(x, y, z, 19.0, 301) * 0.10
        - 0.50
    )
    # A broad ocean between irregular continents, rather than separate scene cards.
    elevation = continental * 13000 - 140
    belt = (
        np.clip((1 - abs(noise3(x, y, z, 12.0, 739) * 2 - 1) - 0.62) / 0.38, 0, 1) ** 2
    )
    uplift = belt * np.clip((continental + 0.01) / 0.12, 0, 1)
    elevation += uplift * 1900
    elevation = np.clip(elevation, -4500, 5200)
    moisture = np.clip(
        noise3(x, y, z, 5.4, 917) * 1.35 - 0.12 + 0.15 * np.cos(lat)[:, None], 0, 1
    )
    moisture *= 1.0 - 0.60 * np.exp(-(((np.abs(lat)[:, None] - 0.42) / 0.16) ** 2))
    q = np.round(elevation).astype("<i2")
    data = (
        q.tobytes()
        + np.round(moisture * 255).astype("u1").tobytes()
        + np.round(uplift * 255).astype("u1").tobytes()
    )
    return struct.pack("<8sII", b"GGWORLD1", width, height) + data


def material(size=512):
    rng = np.random.default_rng(47163)
    # Periodic stochastic material fields; averaged mips suppress distant grit.
    fields = []
    for sigma in (1.1, 3.8, 15, 42):
        a = gaussian_filter(rng.random((size, size)), sigma, mode="wrap")
        fields.append(np.clip((a - a.mean()) / (a.std() * 5) + 0.5, 0, 1))
    a = np.stack([fields[0], fields[1], fields[2], fields[3]], axis=-1)
    return np.round(a * 255).astype("u1")


def measured_relief(source, swiss):
    from bake_terrain import Landscape, SOURCE_SHA256

    if hashlib.sha256(Path(source).read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError("Distant relief source checksum mismatch")

    land = Landscape(source, swiss)
    a = (np.arange(1024) + 0.5) * 8
    x, z = np.meshgrid(a - 1000, a - 4096)
    heights = land.base(x, z)
    detail = heights - gaussian_filter(heights, 128, mode="reflect")
    window = np.sin(np.pi * (np.arange(1024) + 0.5) / 1024) ** 4
    detail *= window[:, None] * window[None, :]
    return (
        struct.pack("<8sIIf", b"GGRELIF1", 1024, 1024, 8.0)
        + np.round(detail).astype("<i2").tobytes()
    )


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--source", type=Path)
    p.add_argument("--swiss", type=Path)
    p.add_argument("--output", type=Path, default=Path("assets/world"))
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    if a.source is not None:
        (a.output / "relief.ggrelief").write_bytes(measured_relief(a.source, a.swiss))
    if not (a.output / "relief.ggrelief").exists():
        p.error("--source is required for the first bake")
    data = build()
    (a.output / "moon.ggworld").write_bytes(data)
    Image.fromarray(material()).save(a.output / "material.png")
    from bake_tree_atlas import build_tree_atlas

    Image.fromarray(build_tree_atlas()).save(a.output / "trees.png")
    manifest = {
        "format": "GGWORLD1",
        "radius_m": 6371000,
        "route_period_real_seconds": 259200,
        "seed": 734,
        "geometry": "Seamless spherical continental elevation and climate; stable meter-scale runtime detail",
        "photographic_input": False,
        "elevation_sha256": hashlib.sha256(data).hexdigest(),
    }
    manifest["measured_relief"] = {
        "source": "Federal Office of Topography swisstopo / Mapzen Skadi",
        "spacing_m": 8,
        "file_sha256": hashlib.sha256(
            (a.output / "relief.ggrelief").read_bytes()
        ).hexdigest(),
        "source_manifest": "../terrain/sources.json",
    }
    (a.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
