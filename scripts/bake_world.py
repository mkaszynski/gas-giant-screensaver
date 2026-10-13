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


def periodic_terrain(heights):
    """Remove only the smooth boundary mismatch, preserving real landforms.

    Original Poisson/FFT implementation of Moisan's periodic-plus-smooth
    decomposition (2011), https://doi.org/10.1007/s10851-010-0227-1 .
    This operates on numerical elevations, not photographs.
    """
    h = np.asarray(heights, dtype=np.float64)
    boundary = np.zeros_like(h)
    boundary[0, :] = h[-1, :] - h[0, :]
    boundary[-1, :] = -boundary[0, :]
    boundary[:, 0] += h[:, -1] - h[:, 0]
    boundary[:, -1] += h[:, 0] - h[:, -1]
    rows, cols = h.shape
    eigen = (
        2 * np.cos(2 * np.pi * np.arange(rows) / rows)[:, None]
        + 2 * np.cos(2 * np.pi * np.arange(cols) / cols)[None, :]
        - 4
    )
    eigen[0, 0] = 1
    spectrum = np.fft.fft2(boundary) / eigen
    spectrum[0, 0] = 0
    relief = h - np.fft.ifft2(spectrum).real
    return relief - relief.mean()


def relief_material(heights, spacing):
    """Residual slope, landform curvature and terrain skylight occlusion."""
    h = np.asarray(heights, dtype=np.float64)
    residual = h - gaussian_filter(h, 2.0, mode="wrap")
    dx = (np.roll(residual, -1, axis=1) - np.roll(residual, 1, axis=1)) / (2 * spacing)
    dz = (np.roll(residual, -1, axis=0) - np.roll(residual, 1, axis=0)) / (2 * spacing)
    length = np.sqrt(1 + dx * dx + dz * dz)
    # Valley/ridge signal at ~250 m, independent of the small crown texture.
    broad = gaussian_filter(h, 8.0, mode="wrap")
    curvature = np.clip(
        0.5 + (broad - gaussian_filter(broad, 16.0, mode="wrap")) / 200.0, 0, 1
    )
    horizon = np.zeros_like(h)
    for x, z in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
        angle = np.zeros_like(h)
        for step in (2, 4, 8, 16, 32, 64):
            neighbor = np.roll(h, (z * step, x * step), axis=(0, 1))
            angle = np.maximum(
                angle, np.arctan2(neighbor - h, spacing * step * np.hypot(x, z))
            )
        horizon += np.cos(angle) ** 2 / 8
    data = np.stack(
        (0.5 - 0.5 * dx / length, 0.5 - 0.5 * dz / length, curvature, horizon), axis=-1
    )
    return np.round(np.clip(data, 0, 1) * 255).astype("u1")


def measured_relief(source, swiss, output):
    from bake_terrain import Landscape, SOURCE_SHA256

    if hashlib.sha256(Path(source).read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError("Distant relief source checksum mismatch")

    land = Landscape(source, swiss)
    # Keep complete 32 km valleys/ridges. The previous 8 km high-pass window
    # and superposition stripped away drainage and made repetitive round hills.
    spacing, size = 16.0, 2048
    a = (np.arange(size) + 0.5 - size * 0.5) * spacing
    x, z = np.meshgrid(a, a)
    heights = land.base(x, z)
    detail = gaussian_filter(periodic_terrain(heights), 0.6, mode="wrap")
    detail *= 0.65
    detail = np.round(detail).astype("<i2")
    Image.fromarray(relief_material(detail, spacing)).save(
        output / "relief-material.png"
    )
    return struct.pack("<8sIIf", b"GGRELIF1", size, size, spacing) + detail.tobytes()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--source", type=Path)
    p.add_argument("--swiss", type=Path)
    p.add_argument("--output", type=Path, default=Path("assets/world"))
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    if a.source is not None:
        (a.output / "relief.ggrelief").write_bytes(
            measured_relief(a.source, a.swiss, a.output)
        )
    if not (a.output / "relief.ggrelief").exists():
        p.error("--source is required for the first bake")
    data = build()
    (a.output / "moon.ggworld").write_bytes(data)
    Image.fromarray(material()).save(a.output / "material.png")
    from bake_canopy import build_canopy

    Image.fromarray(build_canopy()).save(a.output / "canopy.png")
    manifest = {
        "format": "GGWORLD1",
        "radius_m": 6371000,
        "route_period_real_seconds": 259200,
        "seed": 734,
        "geometry": "Seamless spherical continental elevation and climate; stable meter-scale runtime detail",
        "photographic_input": False,
        "canopy_sha256": hashlib.sha256(
            (a.output / "canopy.png").read_bytes()
        ).hexdigest(),
        "elevation_sha256": hashlib.sha256(data).hexdigest(),
    }
    manifest["measured_relief"] = {
        "source": "Federal Office of Topography swisstopo / Mapzen Skadi",
        "spacing_m": 16,
        "landform_span_m": 32768,
        "processing": "Periodic-plus-smooth boundary correction; coherent measured ridges and valleys",
        "material_sha256": hashlib.sha256(
            (a.output / "relief-material.png").read_bytes()
        ).hexdigest(),
        "file_sha256": hashlib.sha256(
            (a.output / "relief.ggrelief").read_bytes()
        ).hexdigest(),
        "source_manifest": "../terrain/sources.json",
    }
    (a.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
