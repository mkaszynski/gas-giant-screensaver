#!/usr/bin/env python3
"""Bake periodic physical surface fields; no photographic or runtime noise input."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

NAMES = ("rock", "grass", "sand", "tundra", "snow", "soil")


def field(rng, size, sigma):
    a = gaussian_filter(rng.random((size, size)), sigma, mode="wrap")
    return np.clip((a - a.mean()) / max(1e-9, a.std() * 4) + 0.5, 0, 1)


def build_material(name, size=512, span=24):
    if name not in NAMES:
        raise ValueError("unknown material")
    rng = np.random.default_rng(79031 + NAMES.index(name))
    broad, fine = field(rng, size, 16), field(rng, size, 2)
    x, z = np.meshgrid(np.arange(size) * span / size, np.arange(size) * span / size)
    # All stored albedos are linear reflectances. Normals derive from actual
    # numerical height; roughness/AO are material properties, not painted light.
    if name == "rock":
        seams = np.minimum(
            np.abs(np.sin(2 * np.pi * (x + 0.7 * np.sin(z * 2 * np.pi / span)) / 3)),
            np.abs(np.sin(2 * np.pi * (z + 0.5 * np.sin(x * 2 * np.pi / span)) / 6)),
        )
        strata = 0.5 + 0.5 * np.sin(
            z * 2 * np.pi / 1.5 + np.sin(x * 2 * np.pi / span) * 2
        )
        h = 0.18 * broad + 0.07 * fine + 0.07 * strata - 0.12 * np.exp(-seams * 25)
        base = np.array([0.19, 0.18, 0.165])
        variation = 0.70 + 0.36 * broad + 0.15 * strata
        rough = 0.72 + 0.16 * fine
    elif name == "grass":
        h = 0.045 * fine + 0.12 * broad
        base = np.array([0.055, 0.092, 0.022])
        variation = 0.70 + 0.60 * broad
        rough = 0.84 + 0.12 * fine
    elif name == "sand":
        ripples = np.sin(2 * np.pi * x / 0.75 + 0.9 * np.sin(2 * np.pi * z / span))
        h = 0.018 * ripples + 0.028 * fine
        base = np.array([0.32, 0.255, 0.17])
        variation = 0.86 + 0.24 * broad
        rough = 0.77 + 0.16 * fine
    elif name == "tundra":
        h = 0.04 * fine + 0.07 * broad
        base = np.array([0.10, 0.09, 0.053])
        variation = 0.68 + 0.65 * broad
        rough = 0.86 + 0.10 * fine
    elif name == "snow":
        h = 0.018 * fine + 0.08 * broad
        base = np.array([0.78, 0.81, 0.83])
        variation = 0.90 + 0.10 * broad
        rough = 0.68 + 0.19 * fine
    else:
        h = 0.08 * broad + 0.06 * fine
        base = np.array([0.08, 0.057, 0.035])
        variation = 0.7 + 0.6 * broad
        rough = 0.86 + 0.10 * fine
    spacing = span / size
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) / (2 * spacing)
    dz = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) / (2 * spacing)
    inv = 1 / np.sqrt(1 + dx * dx + dz * dz)
    cavity = np.clip(0.90 + (h - gaussian_filter(h, 4, mode="wrap")) * 0.8, 0.72, 1)
    albedo = np.concatenate(
        (base[None, None, :] * variation[..., None], rough[..., None]), axis=-1
    )
    normal = np.stack(
        (0.5 - 0.5 * dx * inv, 0.5 - 0.5 * dz * inv, cavity, inv), axis=-1
    )
    return tuple(
        np.round(np.clip(a, 0, 1) * 255).astype("u1") for a in (albedo, normal)
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=Path("assets/world/materials"))
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name in NAMES:
        for suffix, data in zip(("albedo", "normal"), build_material(name)):
            f = a.output / f"{name}-{suffix}.png"
            Image.fromarray(data).save(f)
            hashes[f.name] = hashlib.sha256(f.read_bytes()).hexdigest()
    (a.output / "manifest.json").write_text(
        json.dumps(
            {
                "version": 1,
                "layers": NAMES,
                "size": 512,
                "span_m": 24,
                "albedo_space": "linear",
                "albedo_alpha": "perceptual roughness",
                "normal_channels": "tangent x,z; cavity AO; vertical normal",
                "photographic_input": False,
                "files": hashes,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
