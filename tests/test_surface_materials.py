"""Physical asset invariants, not screenshot tolerances."""

import hashlib
import json
from pathlib import Path
import sys
import unittest
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bake_surface_materials import build_material, NAMES


class SurfaceMaterialTests(unittest.TestCase):
    def test_deterministic_linear_fields(self):
        for name in NAMES:
            a, n = build_material(name, 128)
            b, m = build_material(name, 128)
            np.testing.assert_array_equal(a, b)
            np.testing.assert_array_equal(n, m)
            self.assertEqual(a.shape, (128, 128, 4))
            self.assertGreater(a[:, :, :3].std(), 2)
            self.assertGreater(a[:, :, 3].min(), 128)
            self.assertLessEqual(a[:, :, 3].max(), 255)

    def test_physical_normals_and_cavities(self):
        for name in NAMES:
            _, n = build_material(name, 256)
            horizontal = n[:, :, :2] / 255.0 * 2 - 1
            vertical = n[:, :, 3] / 255.0
            np.testing.assert_allclose(
                (horizontal**2).sum(-1) + vertical**2, 1, atol=0.012
            )
            self.assertGreaterEqual(n[:, :, 2].min(), 180)
            self.assertGreater(n[:, :, 3].min(), 0)

    def test_material_reflectance_order(self):
        means = {
            name: build_material(name, 128)[0][:, :, :3].mean() / 255 for name in NAMES
        }
        self.assertLess(means["grass"], means["rock"])
        self.assertLess(means["rock"], means["sand"])
        self.assertLess(means["sand"], means["snow"])
        self.assertLess(means["soil"], 0.15)
        self.assertGreater(means["snow"], 0.70)
        self.assertLess(means["snow"], 0.9)

    def test_cache_checksums_and_bounded_size(self):
        root = Path(__file__).resolve().parents[1] / "assets/world/materials"
        m = json.loads((root / "manifest.json").read_text())
        self.assertEqual(m["layers"], list(NAMES))
        self.assertEqual(m["albedo_space"], "linear")
        self.assertFalse(m["photographic_input"])
        self.assertEqual(len(m["files"]), 12)
        for name, digest in m["files"].items():
            f = root / name
            self.assertEqual(hashlib.sha256(f.read_bytes()).hexdigest(), digest)
            with Image.open(f) as p:
                self.assertEqual(p.size, (512, 512))
                self.assertEqual(p.mode, "RGBA")
        # Two six-layer RGBA8 arrays with complete mips: 16 MiB total.
        self.assertLessEqual(
            2 * 6 * sum((512 >> i) ** 2 * 4 for i in range(10)), 16 * 1024 * 1024
        )

    def test_unknown_material_rejected(self):
        with self.assertRaises(ValueError):
            build_material("unknown")


if __name__ == "__main__":
    unittest.main()
