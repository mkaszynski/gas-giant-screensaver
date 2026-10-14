#!/usr/bin/env python3
"""Bake touching, irregular tree crowns as filtered height/normal/AO fields."""

import argparse
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter
import hashlib
import json


def canopy_heights(size=1024, metres=512):
    rng = np.random.default_rng(68192)
    cells = 64
    cell = metres / cells
    h = np.zeros((size, size), dtype=np.float64)
    # Periodic maximum of varied overlapping crowns with branch lobes. Dense crowns touch; there
    # is no photograph grain or regularly spaced screen-space dot pattern.
    for z in range(cells):
        for x in range(cells):
            cx = (x + rng.uniform(0.2, 0.8)) * cell
            cz = (z + rng.uniform(0.2, 0.8)) * cell
            rx, rz = rng.uniform(5.5, 8.5, 2)
            height = rng.uniform(9, 16)
            ix = np.arange(
                int((cx - rx) * size / metres), int((cx + rx) * size / metres) + 2
            )
            iz = np.arange(
                int((cz - rz) * size / metres), int((cz + rz) * size / metres) + 2
            )
            dx = ((ix + 0.5) * metres / size - cx) / rx
            dz = ((iz + 0.5) * metres / size - cz) / rz
            radius = dz[:, None] ** 2 + dx[None, :] ** 2
            # Broadleaf domes and tapered conifers have different slope/depth
            # distributions. Branch lobes remove a repeated smooth bubble shape.
            conifer = rng.random() < 0.42
            dome = height * np.clip(1 - radius, 0, 1) ** (0.95 if conifer else 0.5)
            if not conifer:
                for _ in range(3):
                    bx, bz = rng.uniform(-0.45, 0.45, 2)
                    lobe = (
                        height
                        * rng.uniform(0.75, 0.98)
                        * np.sqrt(
                            np.clip(
                                1
                                - ((dx[None, :] - bx) / 0.7) ** 2
                                - ((dz[:, None] - bz) / 0.7) ** 2,
                                0,
                                1,
                            )
                        )
                    )
                    dome = np.maximum(dome, lobe)
            yy, xx = np.meshgrid(iz % size, ix % size, indexing="ij")
            h[yy, xx] = np.maximum(h[yy, xx], dome)
    return gaussian_filter(h, 0.8, mode="wrap")


def build_canopy(size=1024, metres=512):
    h = canopy_heights(size, metres)
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) / (2 * metres / size)
    dz = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) / (2 * metres / size)
    norm = np.sqrt(dx * dx + dz * dz + 1)
    nx, nz = -dx / norm, -dz / norm
    # Occlusion is derived from relative crown depth, with enough dark gaps to
    # distinguish foliage masses while preserving a continuous dense canopy.
    cavity = np.clip((gaussian_filter(h, 5, mode="wrap") - h) / 8, 0, 1)
    ao = np.clip(0.56 + 0.44 * np.clip(h / 15, 0, 1) - cavity * 0.18, 0.4, 1)
    fields = np.stack((nx * 0.5 + 0.5, nz * 0.5 + 0.5, h / 20, ao), axis=-1)
    return np.round(np.clip(fields, 0, 1) * 255).astype("u1")


def crown_visibility(heights, azimuth, metres=512):
    """Directional visibility is filtered *after* occlusion, not before it.

    Mipping heights first erases crown shadows at airplane height. Four solar
    elevations in each of eight azimuth sectors retain their average energy.
    """
    size = heights.shape[0]
    horizon = np.zeros_like(heights)
    for step in (1, 2, 4, 8, 16, 32):
        dx = int(np.round(np.cos(azimuth) * step))
        dz = int(np.round(np.sin(azimuth) * step))
        distance = np.hypot(dx, dz) * metres / size
        neighbor = np.roll(heights, (-dz, -dx), axis=(0, 1))
        horizon = np.maximum(horizon, np.arctan2(neighbor - heights, distance))
    visibility = []
    for elevation in np.deg2rad((5, 25, 55, 85)):
        # A finite sun plus unresolved branch coverage softens silhouettes.
        t = np.clip((elevation - horizon + 0.025) / 0.05, 0, 1)
        visibility.append(t * t * (3 - 2 * t))
    return np.round(np.stack(visibility, -1) * 255).astype("u1")


def write_canopy_assets(output):
    output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(build_canopy()).save(output)
    h = canopy_heights()
    shadow_dir = output.parent / "crown-shadows"
    shadow_dir.mkdir(exist_ok=True)
    shadow_hashes = {}
    for i in range(8):
        target = shadow_dir / f"{i}.png"
        Image.fromarray(crown_visibility(h, i * np.pi / 4)).resize(
            (512, 512), Image.Resampling.BOX
        ).save(target)
        shadow_hashes[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
    (shadow_dir / "manifest.json").write_text(
        json.dumps(
            {
                "version": 1,
                "size": 512,
                "span_m": 512,
                "azimuth_sectors": 8,
                "solar_elevations_degrees": [5, 25, 55, 85],
                "filtering": "visibility averaged after geometric occlusion",
                "files": shadow_hashes,
            },
            indent=2,
        )
        + "\n"
    )
    manifest = output.parent / "manifest.json"
    if manifest.exists():
        m = json.loads(manifest.read_text())
        m["crown_shadows_manifest_sha256"] = hashlib.sha256(
            (shadow_dir / "manifest.json").read_bytes()
        ).hexdigest()
        m["canopy_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
        m["canopy_model"] = (
            "Touching broadleaf lobes and tapered conifers; height-derived normals and cavity occlusion"
        )
        manifest.write_text(json.dumps(m, indent=2) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=Path("assets/world/canopy.png"))
    args = p.parse_args()
    write_canopy_assets(args.output)


if __name__ == "__main__":
    main()
