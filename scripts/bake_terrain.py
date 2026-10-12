#!/usr/bin/env python3
"""Bake a relightable fixed landscape from elevation, never photographic RGB.

Offline only: numpy, scipy and Pillow. Geographic relief is an Earth analogue;
vegetation, sub-grid relief and water are deterministic model approximations.
"""

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import urllib.request

import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates

SOURCE_SHA256 = "cf790a87057dfdf126f9b314d9ec71d407afdedbe71c79e2b14c903d024ce0b9"
SOURCE = "https://elevation-tiles-prod.s3.amazonaws.com/skadi/N46/N46E007.hgt.gz"
VIEW_LAT, VIEW_LON, HEADING = 46.4967, 7.7164, np.deg2rad(95.0)
AZ_MIN, AZ_MAX = np.deg2rad([-65.0, 65.0])
EL_MIN, EL_MAX = np.deg2rad([-12.0, 35.0])
RADIUS = 6371000.0
RELIEF_SCALE = 0.50


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def noise(x, z, size, seed=0):
    """Quintic-interpolated value noise in world meters, bounded [0,1]."""
    x, z = x / size, z / size
    ix, iz = np.floor(x).astype(np.int64), np.floor(z).astype(np.int64)
    fx, fz = x - ix, z - iz
    fx, fz = fx**3 * (fx * (fx * 6 - 15) + 10), fz**3 * (fz * (fz * 6 - 15) + 10)

    def hashed(a, b):
        # Integer overflow is intentional hash arithmetic.
        with np.errstate(over="ignore"):
            n = (
                (a.astype(np.uint32) * np.uint32(1597334677))
                ^ (b.astype(np.uint32) * np.uint32(3812015801))
                ^ np.uint32(seed)
            )
            n ^= n >> 16
            n *= np.uint32(2246822519)
            n ^= n >> 13
        return (n & 0xFFFFFF).astype(np.float32) / 16777215.0

    a, b, c, d = (
        hashed(ix, iz),
        hashed(ix + 1, iz),
        hashed(ix, iz + 1),
        hashed(ix + 1, iz + 1),
    )
    return (a + (b - a) * fx) * (1 - fz) + (c + (d - c) * fx) * fz


def fbm(x, z, scale, octaves=5, seed=17):
    total = np.zeros(np.broadcast_shapes(np.shape(x), np.shape(z)), np.float32)
    weight = 1.0
    norm = 0.0
    for i in range(octaves):
        total += weight * noise(x, z, scale / (2.03**i), seed + 7919 * i)
        norm += weight
        weight *= 0.52
    return total / norm


def canopy(x, z):
    """Deterministic overlapping cone crowns, meters; never drawn at runtime."""
    cell = 5.5
    gx, gz = np.floor(x / cell), np.floor(z / cell)
    crown = np.zeros(np.broadcast_shapes(np.shape(x), np.shape(z)), np.float32)
    for ox in [-1, 0, 1]:
        for oz in [-1, 0, 1]:
            # At cell centers this noise is simply the hash of a grid vertex.
            a = noise((gx + ox) * 17, (gz + oz) * 17, 17, 9241)
            b = noise((gx + ox) * 17, (gz + oz) * 17, 17, 593)
            tx, tz = (gx + ox + 0.18 + 0.64 * a) * cell, (
                gz + oz + 0.18 + 0.64 * b
            ) * cell
            r = np.sqrt((x - tx) ** 2 + (z - tz) ** 2)
            angle = np.arctan2(z - tz, x - tx)
            radius = (2.2 + 1.2 * b) * (
                0.88
                + 0.08 * np.sin(angle * 7 + 13 * a)
                + 0.04 * np.sin(angle * 13 + 9 * b)
            )
            h = (6 + 6 * a) * np.maximum(1 - r / radius, 0) ** 0.72
            crown = np.maximum(crown, h)
    return crown


class Landscape:
    def __init__(self, source, swiss=None, sources_manifest=None):
        self.dem = (
            np.frombuffer(gzip.decompress(Path(source).read_bytes()), ">i2")
            .reshape(3601, 3601)
            .astype(np.float32)
        )
        if np.any(self.dem == -32768):
            raise ValueError("Elevation voids must be repaired before baking")
        self.lat_scale = np.pi * RADIUS / 180
        self.lon_scale = self.lat_scale * np.cos(np.deg2rad(VIEW_LAT))
        self.swiss = None
        if swiss is not None:
            from pyproj import Transformer
            from rasterio.merge import merge
            import rasterio

            paths = sorted(Path(swiss).glob("*.tif"))
            if not paths:
                raise ValueError("Swiss tile directory is empty")
            manifest_path = (
                sources_manifest
                or Path(__file__).resolve().parents[1] / "assets/terrain/sources.json"
            )
            expected = {
                entry["file"]: entry["sha256"]
                for entry in json.loads(Path(manifest_path).read_text())["sources"]
            }
            if set(expected) != {path.name for path in paths}:
                raise ValueError("Swiss input tiles do not match the source manifest")
            for path in paths:
                if hashlib.sha256(path.read_bytes()).hexdigest() != expected[path.name]:
                    raise ValueError("Swiss elevation checksum mismatch: " + path.name)
            datasets = [rasterio.open(path) for path in paths]
            data, transform = merge(datasets, nodata=-9999.0)
            for dataset in datasets:
                dataset.close()
            self.swiss = data[0]
            self.swiss_transform = transform
            project = Transformer.from_crs(4326, 2056, always_xy=True)
            origin = np.array(project.transform(VIEW_LON, VIEW_LAT))
            east = (
                np.array(project.transform(VIEW_LON + 100 / self.lon_scale, VIEW_LAT))
                - origin
            ) / 100
            north = (
                np.array(project.transform(VIEW_LON, VIEW_LAT + 100 / self.lat_scale))
                - origin
            ) / 100
            self.project_origin, self.project_east, self.project_north = (
                origin,
                east,
                north,
            )
        # A modeled alpine lake: a smooth shoreline following the measured basin.
        self.lake = 1579.0
        self.eye = float(self.height(0.0, 0.0)) + 2.3

    def geographic(self, x, z):
        east = x * np.cos(HEADING) + z * np.sin(HEADING)
        north = -x * np.sin(HEADING) + z * np.cos(HEADING)
        return VIEW_LAT + north / self.lat_scale, VIEW_LON + east / self.lon_scale

    def base(self, x, z):
        lat, lon = self.geographic(x, z)
        lat, lon = np.broadcast_arrays(lat, lon)
        result = map_coordinates(
            self.dem,
            [(47.0 - lat).ravel() * 3600, (lon - 7.0).ravel() * 3600],
            order=1,
            mode="nearest",
        ).reshape(lat.shape)
        if self.swiss is not None:
            east = (lon - VIEW_LON) * self.lon_scale
            north = (lat - VIEW_LAT) * self.lat_scale
            point = (
                self.project_origin[:, None]
                + self.project_east[:, None] * east.ravel()
                + self.project_north[:, None] * north.ravel()
            )
            t = self.swiss_transform
            cols, rows = (point[0] - t.c) / t.a - 0.5, (point[1] - t.f) / t.e - 0.5
            detail = map_coordinates(
                self.swiss, [rows, cols], order=1, mode="constant", cval=-9999.0
            ).reshape(lat.shape)
            margin = (
                np.minimum.reduce(
                    [
                        rows,
                        cols,
                        self.swiss.shape[0] - 1 - rows,
                        self.swiss.shape[1] - 1 - cols,
                    ]
                ).reshape(lat.shape)
                * 2
            )
            weight = smooth(0.0, 250.0, margin) * (detail > 0)
            result = result * (1 - weight) + detail * weight
        return result

    def water(self, x, z, h=None):
        lat, lon = self.geographic(x, z)
        # No satellite color or OSM imagery: hand-defined basin extent, clipped
        # by the measured elevation. Not a surveyed shoreline reconstruction.
        basin = ((lon - 7.731) / 0.019) ** 2 + ((lat - 46.499) / 0.0095) ** 2 < 1.0
        if h is None:
            h = self.base(x, z)
        return basin & (h < self.lake) & (x * x + z * z > 140**2)

    def height(self, x, z):
        h = self.base(x, z)
        water = self.water(x, z, h)
        # Meter-scale, zero-mean modeled microrelief. Restrained displacement
        # preserves the measured valleys and silhouette rather than adding spires.
        detail = (noise(x, z, 14.0, 231) - 0.5) * 2.4 + (
            noise(x, z, 5.0, 639) - 0.5
        ) * 0.65
        forest = 1 - smooth(1770.0, 1960.0, h)
        # Preserve exposed low cliffs: a broad slope estimate suppresses trees.
        slope = np.hypot(self.base(x + 12, z) - h, self.base(x, z + 12) - h) / 12
        forest *= 1 - smooth(0.45, 0.90, slope)
        forest *= smooth(750.0, 1050.0, np.hypot(x, z))
        relief = (
            self.lake + (h - self.lake) * RELIEF_SCALE + detail + canopy(x, z) * forest
        )
        # A low modeled outcrop supports the ground observer. It also keeps a
        # dry foreground rim rather than implying the camera floats on the lake.
        island = self.lake + 3 * np.maximum(1 - (x * x + z * z) / 140**2, 0)
        relief = np.where(x * x + z * z < 140**2, island + detail * 0.1, relief)
        return np.where(water, self.lake, relief)

    def surfaces(self, x, z):
        h = self.height(x, z)
        dx = (self.height(x + 2, z) - self.height(x - 2, z)) / 4
        dz = (self.height(x, z + 2) - self.height(x, z - 2)) / 4
        n = np.stack([-dx, np.ones_like(h), -dz], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        broad = fbm(x, z, 320.0, 5)
        fine = fbm(x, z, 18.0, 4, 133)
        climate_height = self.base(x, z)
        bx = (self.base(x + 12, z) - self.base(x - 12, z)) / 24
        bz = (self.base(x, z + 12) - self.base(x, z - 12)) / 24
        true_ny = 1 / np.sqrt(1 + bx * bx + bz * bz)
        rock = smooth(0.28, 0.72, 1 - true_ny)
        rock = np.maximum(
            rock, smooth(2070.0, 2390.0, climate_height + 160 * (broad - 0.5))
        )
        forest = (1 - smooth(1770.0, 1960.0, climate_height)) * (1 - rock)
        forest *= smooth(750.0, 1050.0, np.hypot(x, z))
        meadow = np.array([0.065, 0.085, 0.038]) * (0.72 + 0.65 * broad[..., None])
        canopy = np.array([0.020, 0.037, 0.016]) * (0.48 + 1.05 * fine[..., None])
        stone = np.array([0.170, 0.168, 0.160]) * (0.76 + 0.44 * broad[..., None])
        # Broken bedding and pale weathered faces stay world-space coherent.
        strata = 0.94 + 0.06 * np.sin(h * 0.32 + x * 0.035 + 6 * noise(x, z, 95.0, 74))
        stone *= (strata * (0.78 + 0.45 * fine))[..., None]
        albedo = meadow * (1 - forest[..., None]) + canopy * forest[..., None]
        albedo = albedo * (1 - rock[..., None]) + stone * rock[..., None]
        grit = noise(x, z, 0.32, 502) * 0.12 + noise(x, z, 0.09, 251) * 0.08
        albedo *= (0.90 + grit)[..., None]
        snow = smooth(2680.0, 2860.0, climate_height + 160 * (broad - 0.5)) * smooth(
            0.12, 0.58, n[..., 1]
        )
        albedo = (
            albedo * (1 - snow[..., None])
            + np.array([0.68, 0.71, 0.73]) * snow[..., None]
        )
        # Canopy relief affects normals only at resolutions that can see it.
        cx = (noise(x + 1, z, 7.0, 133) - noise(x - 1, z, 7.0, 133)) * 0.8 * forest
        cz = (noise(x, z + 1, 7.0, 133) - noise(x, z - 1, 7.0, 133)) * 0.8 * forest
        n[..., 0] -= cx
        n[..., 2] -= cz
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        return albedo, n, 1.0 - 0.25 * forest - 0.08 * rock, self.water(x, z)


def ray_profile(land, azimuth, distances):
    x = np.sin(azimuth) * distances
    z = np.cos(azimuth) * distances
    # Curvature matters in far ridges even though the camera itself is fixed.
    elevation = np.arctan(
        (land.height(x, z) - land.eye - distances**2 / (2 * RADIUS)) / distances
    )
    return np.maximum.accumulate(elevation), elevation


def visible_distances(profile, distances, elevation, actual=None):
    at = np.searchsorted(profile, elevation)
    valid = at < len(profile)
    at = np.minimum(at, len(profile) - 1)
    before = np.maximum(at - 1, 0)
    actual = profile if actual is None else actual
    blend = np.clip(
        (elevation - actual[before]) / np.maximum(1.0e-9, actual[at] - actual[before]),
        0,
        1,
    )
    d = distances[before] * (1 - blend) + distances[at] * blend
    return d, valid


def linear_srgb(rgb):
    return np.where(
        rgb <= 0.0031308, rgb * 12.92, 1.055 * np.maximum(rgb, 0) ** (1 / 2.4) - 0.055
    )


def write_rgba(path, data):
    Image.fromarray(np.rint(np.clip(data, 0, 1) * 255).astype(np.uint8)).save(path)


def bake(land, output, width=4096, height=1024):
    output.mkdir(parents=True, exist_ok=True)
    az = AZ_MIN + (np.arange(width) + 0.5) * (AZ_MAX - AZ_MIN) / width
    # Texture y=0 is the lowest elevation. OpenGL PNG uploads keep this order.
    el = EL_MIN + (np.arange(height) + 0.5) * (EL_MAX - EL_MIN) / height
    step = (EL_MAX - EL_MIN) / height
    distances = np.geomspace(2.0, 38000.0, 24000)
    depth = np.empty((height, width), np.float32)
    coverage = np.empty_like(depth)
    for c in range(width):
        profile, original = ray_profile(land, az[c], distances)
        d, valid = visible_distances(profile, distances, el, original)
        # Area coverage of the highest ridge in this angular pixel, before mip
        # filtering. Shade partially covered pixels with the peak's attributes.
        peak = np.argmax(original)
        alpha = np.clip((profile[-1] - el) / step + 0.5, 0, 1)
        d = np.where(valid, d, distances[peak])
        depth[:, c], coverage[:, c] = d, alpha
        if c % 512 == 0:
            print(f"Visibility: {c}/{width}", flush=True)
    albedo = np.empty((height, width, 4), np.float32)
    normal = np.empty_like(albedo)
    geometry = np.empty_like(albedo)
    for c in range(0, width, 64):
        stop = min(width, c + 64)
        d = depth[:, c:stop]
        x, z = np.sin(az[c:stop]) * d, np.cos(az[c:stop]) * d
        rgb, n, ao, water = land.surfaces(x, z)
        alpha = coverage[:, c:stop]
        albedo[:, c:stop, :3] = linear_srgb(rgb * alpha[..., None])
        albedo[:, c:stop, 3] = alpha
        normal[:, c:stop, :3] = n * 0.5 + 0.5
        normal[:, c:stop, 3] = ao
        packed = np.rint(d).astype(np.uint32)
        geometry[:, c:stop, 0] = (packed >> 8) / 255.0
        geometry[:, c:stop, 1] = (packed & 255) / 255.0
        geometry[:, c:stop, 2] = water
        geometry[:, c:stop, 3] = 1
    write_rgba(output / "albedo.png", albedo)
    write_rgba(output / "normal.png", normal)
    write_rgba(output / "geometry.png", geometry)
    # Lower-resolution, eight-direction terrain horizon. This angular
    # approximation replaces runtime shadow maps; directions interpolate smoothly.
    sw, sh = width // 4, height // 4
    shadow = np.zeros((sh, sw, 8), np.float32)
    saz = AZ_MIN + (np.arange(sw) + 0.5) * (AZ_MAX - AZ_MIN) / sw
    sel = EL_MIN + (np.arange(sh) + 0.5) * (EL_MAX - EL_MIN) / sh
    sd = map_coordinates(
        depth,
        [
            np.broadcast_to((np.arange(sh)[:, None] + 0.5) * 4 - 0.5, (sh, sw)),
            np.broadcast_to((np.arange(sw)[None, :] + 0.5) * 4 - 0.5, (sh, sw)),
        ],
        order=1,
    )
    sx, sz = np.sin(saz) * sd, np.cos(saz) * sd
    solid = sel[:, None] < np.arctan((land.height(sx, sz) - land.eye) / sd) + 0.025
    offsets = np.geomspace(12.0, 22000.0, 80)
    for i in range(8):
        for row in range(0, sh, 16):
            a, b = sx[row : row + 16], sz[row : row + 16]
            h0 = land.height(a, b)
            mx = np.zeros_like(a)
            for dist in offsets:
                h1 = land.base(
                    a + dist * np.sin(i * np.pi / 4), b + dist * np.cos(i * np.pi / 4)
                )
                h1 = land.lake + (h1 - land.lake) * RELIEF_SCALE
                mx = np.maximum(
                    mx, np.arctan((h1 - h0 - dist * dist / (2 * RADIUS)) / dist)
                )
            shadow[row : row + 16, :, i] = np.where(
                solid[row : row + 16], mx / (np.pi / 2), 0
            )
        print(f"Horizon: {i+1}/8", flush=True)
    write_rgba(output / "horizon-a.png", shadow[..., :4])
    write_rgba(output / "horizon-b.png", shadow[..., 4:])
    return {
        "size": [width, height],
        "horizon_size": [sw, sh],
        "eye_elevation_m": land.eye,
        "heading_deg": float(np.rad2deg(HEADING)),
        "azimuth_bounds_deg": [-65, 65],
        "elevation_bounds_deg": [-12, 35],
        "lake_elevation_m": land.lake,
        "relief_scale": RELIEF_SCALE,
        "source": SOURCE,
        "source_sha256": None,
        "model": "Measured bare-earth relief; modeled materials, lake mask and microrelief; eight azimuth horizons",
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--swiss", type=Path)
    p.add_argument("--sources-manifest", type=Path)
    p.add_argument("--output", type=Path, default=Path("assets/terrain"))
    p.add_argument("--width", type=int, default=4096)
    p.add_argument("--height", type=int, default=1024)
    args = p.parse_args()
    if not args.source.exists():
        with urllib.request.urlopen(SOURCE, timeout=90) as r:
            args.source.write_bytes(r.read())
    if hashlib.sha256(args.source.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError("Distant elevation source checksum mismatch")
    if (
        args.width < 256
        or args.height < 64
        or args.width > 8192
        or args.height > 2048
        or args.width % 4
        or args.height % 4
    ):
        raise ValueError("Cache sizes must be bounded and divisible by four")
    land = Landscape(args.source, args.swiss, args.sources_manifest)
    manifest = bake(land, args.output, args.width, args.height)
    manifest["cache_sha256"] = {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(args.output.glob("*.png"))
    }
    manifest["observer_coordinates"] = [VIEW_LAT, VIEW_LON]
    manifest["canopy_model"] = (
        "deterministic lobed conifer crowns, 5.5m cells, 6-12m heights; forest begins at 750-1050m range"
    )
    manifest["near_relief"] = "swissALTI3D 2m" if args.swiss else None
    manifest["source_sha256"] = hashlib.sha256(args.source.read_bytes()).hexdigest()
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(manifest, flush=True)


if __name__ == "__main__":
    main()
