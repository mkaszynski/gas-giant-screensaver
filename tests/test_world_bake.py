import hashlib
import json
import struct
import sys
import unittest
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bake_world import build, noise3, material, periodic_terrain, relief_material
from bake_canopy import build_canopy, crown_visibility


class WorldBakeTests(unittest.TestCase):
    def test_boundary_correction_preserves_broad_landforms(self):
        a = np.arange(128) * 2 * np.pi / 128
        h = 600 * np.cos(a)[:, None] + 300 * np.sin(a)[None, :]
        p = periodic_terrain(h)
        self.assertGreater(p.std(), h.std() * 0.90)
        self.assertGreater(np.corrcoef(h.ravel(), p.ravel())[0, 1], 0.99)
        np.testing.assert_array_equal(
            periodic_terrain(np.ones((64, 64)) * 123), np.zeros((64, 64))
        )
        ramp = np.broadcast_to(np.arange(128), (128, 128)).astype(float)
        corrected = periodic_terrain(ramp)
        self.assertLess(np.mean(np.abs(corrected[:, -1] - corrected[:, 0])), 2)

    def test_relief_cache_has_normals_and_real_valley_occlusion(self):
        x = np.arange(128) * 2 * np.pi / 128
        h = 350 * np.cos(x)[None, :] + 180 * np.cos(x)[:, None]
        p = relief_material(h, 16.0)
        self.assertEqual(p.shape, (128, 128, 4))
        np.testing.assert_array_equal(p, relief_material(h, 16.0))
        self.assertGreater(int(p[0, 0, 3]) - int(p[64, 64, 3]), 20)
        self.assertGreater(p[:, :, 2].std(), 5)
        n = p[:, :, :2] / 255.0 * 2 - 1
        self.assertLess((n * n).sum(axis=-1).max(), 1.03)

    def test_deterministic_cache(self):
        a = build(128, 64)
        self.assertEqual(a, build(128, 64))
        self.assertEqual(struct.unpack("<8sII", a[:16]), (b"GGWORLD1", 128, 64))
        self.assertEqual(len(a), 16 + 128 * 64 * 4)

    def test_spherical_noise_continuity(self):
        a = np.array([-0.9, 0.0, 0.9])
        lon = -np.pi + 1.0e-8
        x = np.cos(a) * np.cos(lon)
        y = np.sin(a)
        z = np.cos(a) * np.sin(lon)
        np.testing.assert_allclose(
            noise3(x, y, z, 19, 301), noise3(x, y, -z, 19, 301), atol=1.0e-6
        )

    def test_height_and_climate_ranges(self):
        b = build(256, 128)
        h = np.frombuffer(b[16 : 16 + 256 * 128 * 2], "<i2")
        self.assertLess(h.min(), -1000)
        self.assertGreater(h.max(), 1500)
        self.assertTrue(np.all(h >= -4500))
        self.assertTrue(np.all(h <= 5200))

    def test_material_fields_are_deterministic(self):
        a = material(128)
        np.testing.assert_array_equal(a, material(128))
        self.assertGreater(a.std(), 10)
        self.assertEqual(a.shape, (128, 128, 4))

    def test_canopy_is_dense_filtered_geometry_data(self):
        a = build_canopy(256)
        np.testing.assert_array_equal(a, build_canopy(256))
        self.assertEqual(a.shape, (256, 256, 4))
        self.assertGreater((a[:, :, 2] > 50).mean(), 0.98)
        self.assertGreater(a[:, :, 2].std(), 15)
        normals = a[:, :, :2] / 255.0 * 2 - 1
        self.assertLessEqual((normals**2).sum(axis=-1).max(), 1.03)
        self.assertGreaterEqual(a[:, :, 3].min(), 102)
        self.assertGreater(a[:, :, 3].std(), 12)

    def test_directional_crown_shadows_preserve_filtered_visibility(self):
        flat = np.zeros((128, 128))
        np.testing.assert_array_equal(
            crown_visibility(flat, 0, 128), np.full((128, 128, 4), 255, dtype="u1")
        )
        wall = flat.copy()
        wall[:, 64:] = 15
        towards = crown_visibility(wall, 0, 128)
        sideways = crown_visibility(wall, np.pi / 2, 128)
        self.assertEqual(int(towards[64, 40, 0]), 0)
        self.assertEqual(int(towards[64, 40, 3]), 255)
        self.assertEqual(int(sideways[64, 40, 0]), 255)
        self.assertTrue((np.diff(towards.astype(int), axis=-1) >= 0).all())
        self.assertLess(towards[:, :, 0].mean(), 255)
        root = Path(__file__).resolve().parents[1] / "assets/world/crown-shadows"
        manifest = json.loads((root / "manifest.json").read_text())
        self.assertEqual(manifest["solar_elevations_degrees"], [5, 25, 55, 85])
        self.assertEqual(len(manifest["files"]), 8)
        for file, digest in manifest["files"].items():
            self.assertEqual(
                hashlib.sha256((root / file).read_bytes()).hexdigest(), digest
            )

    def test_committed_asset_checksums(self):
        root = Path(__file__).resolve().parents[1] / "assets/world"
        m = json.loads((root / "manifest.json").read_text())
        self.assertEqual(
            hashlib.sha256((root / "moon.ggworld").read_bytes()).hexdigest(),
            m["elevation_sha256"],
        )
        self.assertEqual(
            hashlib.sha256((root / "relief.ggrelief").read_bytes()).hexdigest(),
            m["measured_relief"]["file_sha256"],
        )
        self.assertEqual(m["route_period_real_seconds"], 259200)
        self.assertEqual(
            hashlib.sha256(
                (root / "crown-shadows/manifest.json").read_bytes()
            ).hexdigest(),
            m["crown_shadows_manifest_sha256"],
        )
        self.assertEqual(
            hashlib.sha256((root / "relief-material.png").read_bytes()).hexdigest(),
            m["measured_relief"]["material_sha256"],
        )
        self.assertEqual(
            hashlib.sha256((root / "canopy.png").read_bytes()).hexdigest(),
            m["canopy_sha256"],
        )

    def test_cached_circuit_is_bound_to_current_world(self):
        root = Path(__file__).resolve().parents[1] / "assets/world"
        m = json.loads((root / "manifest.json").read_text())
        cache = (root / "flight.ggflight").read_bytes()
        magic, count = struct.unpack("<8sI", cache[:12])
        self.assertEqual(magic, b"GGFLY001")
        self.assertEqual(len(cache), 12 + 4 * count)
        offsets = np.frombuffer(cache[12:], "<f4")
        self.assertTrue(np.isfinite(offsets).all())
        self.assertGreaterEqual(offsets.min(), 600)
        self.assertLessEqual(offsets.max(), 12000)
        self.assertLess(np.max(np.abs(np.diff(np.r_[offsets, offsets[0]]))), 27)
        self.assertEqual(m["flight"]["maximum_detour_m"], 0)
        self.assertEqual(hashlib.sha256(cache).hexdigest(), m["flight"]["file_sha256"])
        self.assertEqual(count, m["flight"]["points"])
        self.assertEqual(m["flight"]["world_sha256"], m["elevation_sha256"])
        self.assertEqual(
            m["flight"]["relief_sha256"], m["measured_relief"]["file_sha256"]
        )


if __name__ == "__main__":
    unittest.main()
