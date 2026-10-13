import hashlib
import json
import struct
import sys
import unittest
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bake_world import build, noise3, material
from bake_tree_atlas import build_tree_atlas


class WorldBakeTests(unittest.TestCase):
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

    def test_foliage_has_fine_cutouts(self):
        a = build_tree_atlas()
        np.testing.assert_array_equal(a, build_tree_atlas())
        self.assertEqual(a.shape, (512, 2048, 4))
        for half in (a[:, :1024], a[:, 1024:]):
            self.assertGreater((half[:, :, 3] == 255).sum(), 20000)
            self.assertGreater((half[:, :, 3] == 0).sum(), 100000)

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

    def test_cached_circuit_is_bound_to_current_world(self):
        root = Path(__file__).resolve().parents[1] / "assets/world"
        m = json.loads((root / "manifest.json").read_text())
        cache = (root / "circuit.ggroute").read_bytes()
        magic, count = struct.unpack("<8sI", cache[:12])
        self.assertEqual(magic, b"GGROUTE1")
        self.assertEqual(len(cache), 12 + 4 * count)
        offsets = np.frombuffer(cache[12:], "<f4")
        self.assertTrue(np.isfinite(offsets).all())
        self.assertLessEqual(np.max(np.abs(offsets)), 4992)
        self.assertEqual(offsets[0], 0)
        self.assertEqual(offsets[-1], 0)
        self.assertLessEqual(np.max(np.abs(np.diff(offsets))), 192)
        self.assertEqual(hashlib.sha256(cache).hexdigest(), m["circuit"]["file_sha256"])
        self.assertEqual(count, m["circuit"]["points"])
        self.assertEqual(m["circuit"]["world_sha256"], m["elevation_sha256"])
        self.assertEqual(
            m["circuit"]["relief_sha256"], m["measured_relief"]["file_sha256"]
        )


if __name__ == "__main__":
    unittest.main()
