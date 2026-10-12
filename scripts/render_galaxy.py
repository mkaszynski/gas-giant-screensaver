#!/usr/bin/env python3
"""Render a deterministic HDR galaxy from numeric CSFD + Edenhofer dust data.
Offline dependencies: numpy, scipy, astropy, healpy, ispc_texcomp==1.0.1.
No RGB sky photograph or NASA raster is read. See docs/GALAXY.md.
"""

import argparse
import hashlib
import json
import mmap
import os
import struct
from pathlib import Path
import importlib.metadata

import healpy as hp
import ispc_texcomp as encoder
import numpy as np
from astropy.io import fits
from galaxy_model import RadianceModel, world_to_galactic

FACES = ("px", "nx", "py", "ny", "pz", "nz")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        while b := f.read(1024 * 1024):
            h.update(b)
    return h.hexdigest()


def prepare_dust(mean_path, metadata_path, output):
    """Integrate all measured radial shells; retain 48 cumulative depth knots.

    mean_path contains exactly the original primary/MEAN HDUs, fetched using
    verified HTTP byte ranges. The metadata subset starts at the radial-center
    HDU, after the unused STD HDU. No standard-deviation data is fabricated.
    """
    if (
        mean_path.stat().st_size != 1623202560
        or metadata_path.stat().st_size != 6312960
    ):
        raise ValueError("Unexpected pinned Edenhofer v1.0 subset lengths")
    raw = metadata_path.read_bytes()
    center = np.frombuffer(raw, dtype=">f4", count=516, offset=2880).astype(float)
    bounds = np.frombuffer(raw, dtype=">f4", count=517, offset=8640).astype(float)
    inner = np.frombuffer(raw, dtype=">f4", count=786432, offset=14400).astype(
        np.float32
    )
    if not (np.diff(bounds) > 0).all() or not (np.diff(center) > 0).all():
        raise ValueError("Invalid dust distances")
    mean = np.memmap(mean_path, mode="r", dtype=">f4", offset=5760, shape=(516, 786432))
    selected = np.unique(np.rint(np.linspace(0, 515, 48)).astype(int))
    columns = []
    radii = []
    column = np.maximum(inner, 0)
    columns.append(column.copy())
    radii.append(bounds[0] / 1000)
    for row in range(516):
        density = np.asarray(mean[row], dtype=np.float32)
        if not np.isfinite(density).all() or (density < 0).any():
            raise ValueError("Invalid measured dust density")
        column += density * (bounds[row + 1] - bounds[row])
        if row in selected:
            columns.append(column.copy())
            radii.append(bounds[row + 1] / 1000)
        if row % 32 == 31:
            mean._mmap.madvise(mmap.MADV_DONTNEED)
    np.savez_compressed(output, radii=np.asarray(radii), columns=np.stack(columns))
    return {
        "mean_subset_sha256": digest(mean_path),
        "metadata_subset_sha256": digest(metadata_path),
    }


def repair_column_holes(column, nside):
    """Fill zero-valued conversion holes, without changing measured pixels.

    The pinned CSFD map has 496 exact zeros on the Galactic-plane conversion
    seam. Treating them as transparent exposes an artificial unextincted bar.
    Neighbor medians reconstruct those missing columns deterministically.
    Small negative measurement fluctuations elsewhere remain clamped to zero
    at query time, rather than being mistaken for the exact-zero seam.
    """
    missing = np.flatnonzero(column == 0)
    pending = missing.copy()
    for _ in range(64):
        if not len(pending):
            return missing
        neighbors = hp.get_all_neighbours(nside, pending)
        values = column[np.maximum(neighbors, 0)].astype(float)
        values[(neighbors < 0) | (values <= 0)] = np.nan
        ready = np.any(np.isfinite(values), axis=0)
        if not ready.any():
            break
        column[pending[ready]] = np.nanmedian(values[:, ready], axis=0)
        pending = pending[~ready]
    raise ValueError("Dust column has holes without valid neighboring measurements")


class DustField:
    def __init__(self, csfd_path, profile_path):
        with fits.open(csfd_path, memmap=True) as f:
            if (
                f[1].header["NSIDE"] != 2048
                or f[1].header["ORDERING"].strip() != "RING"
            ):
                raise ValueError("Unexpected CSFD projection")
            self.column = np.asarray(f[1].data["T"].ravel(), dtype=np.float32)
        self.repaired_pixels = repair_column_holes(self.column, 2048)
        p = np.load(profile_path)
        self.radii = p["radii"]
        self.profiles = p["columns"]
        if not np.isfinite(self.column).all() or not np.isfinite(self.profiles).all():
            raise ValueError("Invalid dust field")

    def extinction(self, direction, distance):
        lon = np.arctan2(direction[:, 1], direction[:, 0]) % (2 * np.pi)
        theta = np.arccos(np.clip(direction[:, 2], -1, 1))
        total = np.maximum(hp.get_interp_val(self.column, theta, lon), 0) * 2.742
        pixels, weights = hp.get_interp_weights(256, theta, lon, nest=True)
        profile = np.zeros((len(self.radii), len(direction)))
        for i in range(4):
            profile += self.profiles[:, pixels[i]] * weights[i][None, :]
        # Zhang/Green/Rix E -> approximate Johnson V extinction (2.8 E).
        local = profile * 2.8
        # The two surveys have different resolutions/normalizations. Constrain
        # local extinction by the measured all-sky column, without negative residuals.
        local *= np.minimum(1, total / np.maximum(local[-1], 1e-10))[None, :]
        idx = np.searchsorted(self.radii, distance)
        high = np.clip(idx, 0, len(self.radii) - 1)
        low = np.clip(idx - 1, 0, len(self.radii) - 1)
        f = np.where(
            idx == 0,
            distance / self.radii[0],
            (distance - self.radii[low])
            / np.maximum(self.radii[high] - self.radii[low], 1e-10),
        )
        f = np.clip(f, 0, 1)
        before = np.where(idx[None, :] == 0, 0, local[low].T)
        av = before * (1 - f)[None, :] + local[high].T * f[None, :]
        # Beyond measured 1.25 kpc, distribute the remaining observed column in
        # a smooth gas disk. This is a documented distance-model approximation.
        s = np.asarray(distance)[None, :]
        xyz = direction[:, None, :] * s[:, :, None]
        radius = np.hypot(8.2 - xyz[..., 0], xyz[..., 1])
        height = 0.020 + xyz[..., 2]
        gas = np.exp((8.2 - radius) / 3.2 - np.abs(height) / 0.12) * (
            s > self.radii[-1]
        )
        edges = np.geomspace(0.001, 30.0, len(distance) + 1)
        cumulative = np.cumsum(gas * np.diff(edges)[None, :], axis=1)
        far = cumulative / np.maximum(cumulative[:, -1:], 1e-20)
        av += (total - local[-1])[:, None] * far
        return np.maximum(av, 0)


def face_directions(face, size, start, end):
    s, t = np.meshgrid(
        (np.arange(size) + 0.5) * 2 / size - 1,
        (np.arange(start, end) + 0.5) * 2 / size - 1,
    )
    one = np.ones_like(s)
    axes = (
        (one, -t, -s),
        (-one, -t, s),
        (s, one, t),
        (s, -one, -t),
        (s, -t, one),
        (-s, -t, -one),
    )
    world = np.stack(axes[face], axis=-1).reshape(-1, 3)
    return world_to_galactic(world / np.linalg.norm(world, axis=1)[:, None])


def next_mip(rgb):
    n = rgb.shape[0]
    if n == 1:
        return rgb
    return (
        rgb.reshape(n // 2, 2, n // 2, 2, 3)
        .astype(np.float32)
        .mean(axis=(1, 3))
        .astype(np.float16)
    )


def encode_face(rgb, path):
    settings = encoder.BC6HEncSettings.from_profile("slow")
    size = rgb.shape[0]
    temporary = path.with_suffix(".tmp")
    with temporary.open("wb") as out:
        out.write(b"GGSHDR01" + struct.pack("<II", size, size.bit_length()))
        while True:
            n = rgb.shape[0]
            padded = np.pad(rgb, ((0, (-n) % 4), (0, (-n) % 4), (0, 0)), mode="edge")
            rgba = np.ones((*padded.shape[:2], 4), dtype=np.float16)
            rgba[..., :3] = padded
            surface = encoder.RGBASurface(
                rgba.tobytes(), rgba.shape[1], rgba.shape[0], stride=rgba.shape[1] * 8
            )
            blocks = encoder.compress_blocks_bc6h(surface, settings)
            if len(blocks) != ((n + 3) // 4) ** 2 * 16:
                raise ValueError("BC6H output length")
            out.write(struct.pack("<I", len(blocks)))
            out.write(blocks)
            if n == 1:
                break
            rgb = next_mip(rgb)
    temporary.replace(path)


def write_reference(rgb, path):
    with path.open("wb") as out:
        size = rgb.shape[0]
        out.write(b"GGSRAW01" + struct.pack("<II", size, size.bit_length()))
        while True:
            data = rgb.astype("<f2").tobytes()
            out.write(struct.pack("<I", len(data)))
            out.write(data)
            if rgb.shape[0] == 1:
                break
            rgb = next_mip(rgb)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dust", type=Path, required=True)
    p.add_argument("--profiles", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument(
        "--face-size", type=int, choices=(256, 512, 1024, 2048, 4096), default=2048
    )
    p.add_argument("--steps", type=int, default=96)
    p.add_argument("--faces", nargs="+", choices=FACES, default=FACES)
    p.add_argument(
        "--reference",
        type=Path,
        help="Keep uncompressed linear half-float .ggsky faces for comparisons",
    )
    a = p.parse_args()
    if importlib.metadata.version("ispc_texcomp") != "1.0.1":
        p.error("Use the pinned ispc_texcomp==1.0.1")
    if len(set(a.faces)) != len(a.faces):
        p.error("Each face may be selected only once")
    a.output.mkdir(parents=True, exist_ok=True)
    if a.reference:
        a.reference.mkdir(parents=True, exist_ok=True)
    dust = DustField(a.dust, a.profiles)
    model = RadianceModel(a.steps)
    for name in a.faces:
        face = FACES.index(name)
        rgb = np.zeros((a.face_size, a.face_size, 3), dtype=np.float16)
        # Bounded ray batches; never allocate a full sky x depth array.
        rows = max(1, 4096 // a.face_size)
        for start in range(0, a.face_size, rows):
            end = min(start + rows, a.face_size)
            direction = face_directions(face, a.face_size, start, end)
            values = model.integrate(
                direction, dust.extinction(direction, model.distance)
            )
            if np.max(values) > 65504:
                raise ValueError("Model exceeds half-float range")
            rgb[start:end] = values.reshape(end - start, a.face_size, 3)
        if a.reference:
            write_reference(rgb, a.reference / f"galaxy-{name}.ggsky")
        encode_face(rgb, a.output / f"galaxy-{name}.ggsky")
        print(
            "Rendered HDR",
            name,
            "range",
            float(rgb.min()),
            float(rgb.max()),
            flush=True,
        )
    manifest = {
        "model": "deterministic disk/bar stellar radiance with measured local 3D + all-sky dust",
        "face_size": a.face_size,
        "integration_steps": a.steps,
        "dust_zero_column_repairs": len(dust.repaired_pixels),
        "resolved_limit": 6.5,
        "radiance_units": "V-zero-point-relative flux per steradian",
        "gpu_format": "BC6H unsigned linear HDR",
        "encoder": {
            "name": "ispc_texcomp",
            "version": "1.0.1",
            "profile": "slow",
            "mip_filter": "linear radiance box",
        },
        "sources": {a.dust.name: digest(a.dust), a.profiles.name: digest(a.profiles)},
        "input_subsets": {
            "Edenhofer_primary_and_MEAN_sha256": "4bad79d417107c1a090779bb60d45b1fd4a2f5716797f34b58b5180f69cbf984",
            "Edenhofer_radial_and_inner_metadata_sha256": "397bcb45b72d444ec20ac33e5f1f3d6c12162d512add1a5a5251890be9d43cac",
        },
    }
    (a.output / f"model-manifest-{os.getpid()}.tmp").write_text(
        json.dumps(manifest, indent=2) + "\n"
    )

    (a.output / f"model-manifest-{os.getpid()}.tmp").replace(
        a.output / "model-manifest.json"
    )


if __name__ == "__main__":
    main()
