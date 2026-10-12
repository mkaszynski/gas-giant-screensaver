"""Behavior tests for offline radiance synthesis; runs without source downloads."""

import sys
import tempfile
import unittest
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from galaxy_model import (
    RadianceModel,
    world_to_galactic,
    unresolved_fraction,
    spectral_lut,
)
from render_galaxy import (
    DustField,
    next_mip,
    write_reference,
    encode_face,
    repair_column_holes,
)


class ModelTests(unittest.TestCase):
    def test_equatorial_galactic_orientation(self):
        ra = np.radians(266.4051)
        dec = np.radians(-28.936175)
        tilt = np.radians(23.4392911)
        x = np.cos(dec) * np.cos(ra)
        y = np.sin(dec)
        z = np.cos(dec) * np.sin(ra)
        world = np.array(
            [
                [
                    x,
                    y * np.cos(tilt) - z * np.sin(tilt),
                    y * np.sin(tilt) + z * np.cos(tilt),
                ]
            ]
        )
        np.testing.assert_allclose(world_to_galactic(world), [[1, 0, 0]], atol=2e-6)

    def test_reproducibility_and_real_dynamic_range(self):
        m = RadianceModel()
        d = np.array([[1.0, 0, 0], [0, 0, 1.0]])
        av = np.zeros((2, len(m.distance)))
        first = m.integrate(d, av)
        np.testing.assert_array_equal(first, m.integrate(d, av))
        self.assertGreater(first[0, 1], first[1, 1] * 100)
        self.assertGreater(first.max(), 1)  # No display clipping or 8-bit encoding.
        self.assertTrue(np.isfinite(first).all())

    def test_dust_absorbs_and_reddens(self):
        m = RadianceModel()
        d = np.array([[0.0, 1, 0]])
        clear = m.integrate(d, np.zeros((1, len(m.distance))))[0]
        obscured = m.integrate(d, np.full((1, len(m.distance)), 2.0))[0]
        self.assertTrue((obscured < clear).all())
        self.assertGreater(obscured[0] / obscured[2], clear[0] / clear[2])

    def test_luminosity_budget_split(self):
        f = unresolved_fraction(np.geomspace(0.001, 30, 128), 0)
        self.assertTrue(((f >= 0) & (f <= 1)).all())
        self.assertLess(f[0], 1e-4)
        self.assertGreater(f[-1], 0.999)
        np.testing.assert_allclose(f + (1 - f), 1, rtol=0, atol=1e-15)
        # Extinction can move a nearby bright population below the point limit.
        self.assertGreater(unresolved_fraction(0.05, 2), unresolved_fraction(0.05, 0))

    def test_quadrature_converges(self):
        d = np.array([[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0], [-1.0, 0, 0]])
        results = []
        for steps in (96, 192):
            m = RadianceModel(steps)
            av = np.broadcast_to(0.7 * (1 - np.exp(-m.distance / 0.2)), (4, steps))
            results.append(m.integrate(d, av))
        np.testing.assert_allclose(results[0], results[1], rtol=0.02)

    def test_continuous_intrinsic_field(self):
        m = RadianceModel()
        d = np.array([[1.0, 0.01, 0.01], [1.0, 0.010001, 0.01]])
        d /= np.linalg.norm(d, axis=1)[:, None]
        p = m.integrate(d, np.zeros((2, len(m.distance))))
        self.assertLess(np.max(abs(p[0] - p[1]) / p[0]), 0.001)

    def test_spectral_templates(self):
        for t in (4500, 8500):
            rgb = spectral_lut(t)
            self.assertAlmostEqual(
                rgb[0] @ np.array([0.2126, 0.7152, 0.0722]), 1, places=12
            )
            self.assertTrue((rgb >= 0).all())
            self.assertLess(rgb[-1].max(), 1e-8)
            self.assertGreater(rgb[16, 0] / rgb[16, 2], rgb[0, 0] / rgb[0, 2])

    def test_dust_depth_and_column_conservation(self):
        dust = DustField.__new__(DustField)
        dust.column = np.full(12, 1.0, dtype=np.float32)  # Uniform HEALPix nside=1.
        dust.radii = np.array([0.068, 0.5, 1.25])
        dust.profiles = np.broadcast_to(
            np.array([0.01, 0.1, 0.3])[:, None], (3, 786432)
        )
        m = RadianceModel()
        d = np.array([[1.0, 0, 0], [0.0, 0, 1.0]])
        av = dust.extinction(d, m.distance)
        self.assertTrue((av >= 0).all())
        self.assertTrue((np.diff(av, axis=1) >= -1e-10).all())
        self.assertTrue((av <= 2.742 + 1e-6).all())

        self.assertLess(av[:, m.distance < 0.05].max(), 0.03)
        np.testing.assert_allclose(av[:, -1], 2.742, rtol=1e-6)
        dust.profiles = (
            dust.profiles * 100
        )  # Inconsistent surveys cannot add negative far dust.
        av = dust.extinction(d, m.distance)
        self.assertTrue((av >= 0).all())
        self.assertTrue((av <= 2.742 + 1e-6).all())

    def test_missing_dust_columns_are_not_transparent_windows(self):
        column = np.full(12 * 16**2, 2.0, dtype=np.float32)
        column[100:120] = 0
        measured = column.copy()
        repaired = repair_column_holes(column, 16)
        np.testing.assert_array_equal(repaired, np.arange(100, 120))
        np.testing.assert_array_equal(column, 2)
        np.testing.assert_array_equal(column[measured != 0], measured[measured != 0])
        self.assertRaises(ValueError, repair_column_holes, np.zeros(12), 1)

    def test_hdr_mips_and_reference_serialization(self):
        a = np.full((4, 4, 3), 1000.0, dtype=np.float16)
        a[:2, :2] = 0.00001
        b = next_mip(a)
        np.testing.assert_array_equal(b[0, 0], a[0, 0])
        self.assertEqual(float(b[1, 1, 0]), 1000)
        self.assertAlmostEqual(
            float(b.astype(float).sum() * 4 / a.astype(float).sum()), 1, places=6
        )
        with tempfile.TemporaryDirectory() as p:
            path = Path(p) / "ref.ggsky"
            write_reference(a, path)
            data = path.read_bytes()
            self.assertEqual(data[:8], b"GGSRAW01")
            rgb = np.frombuffer(data, dtype="<f2", offset=20, count=48).reshape(4, 4, 3)
            np.testing.assert_array_equal(rgb, a)
            encode_face(a, Path(p) / "compressed.ggsky")
            self.assertEqual(
                (Path(p) / "compressed.ggsky").read_bytes()[:8], b"GGSHDR01"
            )

    def test_invalid_integrator_inputs(self):
        self.assertRaises(ValueError, RadianceModel, 1)
        m = RadianceModel()
        d = np.array([[1.0, 0, 0]])
        for av in [np.zeros((1, 3)), np.full((1, 96), -1), np.full((1, 96), np.nan)]:
            self.assertRaises(ValueError, m.integrate, d, av)


if __name__ == "__main__":
    unittest.main()
