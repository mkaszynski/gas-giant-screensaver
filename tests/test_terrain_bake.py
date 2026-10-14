"""Offline geometry/material behavior; synthetic elevation, no downloads."""

import sys
from pathlib import Path
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bake_terrain import (
    noise,
    fbm,
    visible_distances,
    ray_profile,
    Landscape,
    RADIUS,
    RELIEF_SCALE,
    canopy,
)


class Plane(Landscape):
    def __init__(self):
        self.swiss = None
        self.eye, self.lake = 1600.0, 1578.0

    def base(self, x, z):
        return np.broadcast_to(
            1500.0 + np.asarray(x) * 0.1 + np.asarray(z) * 0.05,
            np.broadcast_shapes(np.shape(x), np.shape(z)),
        )

    def water(self, x, z, h=None):
        return np.zeros(np.broadcast_shapes(np.shape(x), np.shape(z)), bool)


class TerrainTests(unittest.TestCase):
    def test_world_noise_is_bounded_repeatable_and_continuous(self):
        x = np.linspace(-500, 500, 1001)
        a = noise(x, x * 0.7, 7.0, 5)
        self.assertTrue(np.array_equal(a, noise(x, x * 0.7, 7.0, 5)))
        self.assertTrue(np.all((a >= 0) & (a <= 1)))
        self.assertLess(np.max(abs(a - noise(x + 1e-4, x * 0.7, 7.0, 5))), 1e-4)
        self.assertGreater(np.std(fbm(x, x * 0.7, 80.0)), 0.03)

    def test_plane_normals_are_upward_unit_and_tilted(self):
        model = Plane()
        x = np.linspace(0, 2000, 100)
        rgb, n, ao, water = model.surfaces(x, x)
        self.assertTrue(np.all(np.isfinite(rgb)))
        self.assertTrue(np.all((rgb >= 0) & (rgb <= 1)))
        self.assertTrue(np.allclose(np.linalg.norm(n, axis=-1), 1.0, atol=1e-6))
        self.assertTrue(np.all(n[:, 1] > 0))
        self.assertLess(np.mean(n[:, 0]), 0)
        self.assertTrue(np.all((ao > 0) & (ao <= 1)))
        self.assertFalse(np.any(water))

    def test_visibility_returns_first_intersection(self):
        distances = np.array([1.0, 2.0, 3.0, 4.0, 5.0])
        profile = np.array([-0.7, -0.5, -0.5, -0.2, 0.1])
        d, valid = visible_distances(
            profile, distances, np.array([-0.6, -0.4, 0.0, 0.2])
        )
        self.assertTrue(np.allclose(d[:3], [1.5, 3.0 + 1 / 3, 4.0 + 2 / 3]))
        self.assertTrue(np.array_equal(valid, [True, True, True, False]))

    def test_curvature_and_occlusion_envelope(self):
        model = Plane()
        d = np.geomspace(2.0, 20000.0, 200)
        p, e = ray_profile(model, 0.0, d)
        self.assertTrue(np.all(np.diff(p) >= 0))
        self.assertTrue(np.all(p >= e))
        expected = np.arctan(
            (model.height(0.0, d) - model.eye - d * d / (2 * RADIUS)) / d
        )
        self.assertTrue(np.array_equal(e, expected))

    def test_tree_crown_slope_cannot_turn_forest_into_rock(self):
        model = Plane()
        x = np.linspace(900.0, 980.0, 100)
        rgb, normal, ao, water = model.surfaces(x, np.full_like(x, 900.0))
        self.assertTrue(np.all(rgb[:, 1] > rgb[:, 0] * 1.6))

    def test_canopy_is_static_and_meter_scale(self):
        x = np.linspace(-200, 200, 500)
        a = canopy(x, x * 0.4)
        self.assertTrue(np.array_equal(a, canopy(x, x * 0.4)))
        self.assertTrue(np.all((a >= 0) & (a <= 12)))
        self.assertGreater(a.max(), 5.0)
        self.assertLess(np.max(abs(a - canopy(x + 1e-4, x * 0.4))), 0.01)

    def test_depth_uses_the_local_segment_not_occlusion_envelope(self):
        d = np.arange(1.0, 6.0)
        actual = np.array([-0.7, -0.5, -0.9, -0.2, 0.1])
        depth, valid = visible_distances(
            np.maximum.accumulate(actual), d, np.array([-0.4]), actual
        )
        self.assertTrue(valid[0])
        self.assertAlmostEqual(depth[0], 3 + 5 / 7)

    def test_relief_is_restrained(self):
        model = Plane()
        self.assertGreater(RELIEF_SCALE, 0)
        self.assertLessEqual(RELIEF_SCALE, 1)
        x = np.linspace(300, 5000, 200)
        expected = model.lake + (model.base(x, x) - model.lake) * RELIEF_SCALE
        self.assertLess(np.max(abs(model.height(x, x) - expected)), 14.0)


if __name__ == "__main__":
    unittest.main()
