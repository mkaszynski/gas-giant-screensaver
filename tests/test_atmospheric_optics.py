"""Physical geometry and the committed spectral scattering asset."""

from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bake_atmospheric_optics import water_index, prism_pdf, solar_filter, WIDTH, HEIGHT

ROOT = Path(__file__).resolve().parents[1]


class OpticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / "assets/world/optics/phase.ggoptics"
        cls.raw = path.read_bytes()
        cls.table = (
            np.frombuffer(cls.raw[16:], dtype="<f2")
            .astype(float)
            .reshape(HEIGHT, WIDTH, 4)
        )

    def test_schema_and_hash(self):
        m = json.loads((ROOT / "assets/world/optics/manifest.json").read_text())
        self.assertEqual(self.raw[:8], b"GGOPT01\0")
        self.assertEqual(struct.unpack("<II", self.raw[8:16]), (WIDTH, HEIGHT))
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), m["sha256"])
        self.assertTrue(np.isfinite(self.table).all())
        self.assertGreaterEqual(self.table.min(), 0.0)
        self.assertTrue((self.table[:, :, 3] == 1).all())

    def test_water_dispersion(self):
        self.assertAlmostEqual(float(water_index(0.5893)), 1.3330, places=3)
        self.assertGreater(float(water_index(0.45)), float(water_index(0.65)))

    def test_prism_geometry_and_probability(self):
        n = 1.311
        angles = (np.arange(WIDTH) + 0.5) * np.pi / WIDTH
        pdf = prism_pdf(n)
        minimum = 2 * np.arcsin(n * 0.5) - np.pi / 3
        self.assertAlmostEqual(pdf.sum() * np.pi / WIDTH, 1.0, places=10)
        self.assertLess(abs(angles[pdf.argmax()] - minimum), np.radians(0.12))
        self.assertEqual(float(pdf[angles < minimum - np.pi / WIDTH].sum()), 0.0)
        high = prism_pdf(n, np.radians(40))
        self.assertGreater(angles[high.argmax()], angles[pdf.argmax()])
        self.assertEqual(float(prism_pdf(n, np.radians(70)).sum()), 0.0)
        self.assertLess(
            prism_pdf(n, np.radians(60), normalized=False).sum(),
            0.1 * prism_pdf(n, normalized=False).sum(),
        )

    def test_halo_color_order(self):
        angles = (np.arange(WIDTH) + 0.5) * 180 / WIDTH
        region = (angles > 20) & (angles < 25)
        peaks = [angles[region][self.table[0, region, c].argmax()] for c in range(3)]
        self.assertTrue(20.5 < peaks[0] < peaks[1] < peaks[2] < 24.0, peaks)

    def test_parhelia_move_outward(self):
        angles = (np.arange(WIDTH) + 0.5) * 180 / WIDTH
        peak = lambda h: angles[self.table[1 + h, :, 1].argmax()]
        self.assertTrue(21.0 < peak(0) < 24.0)
        self.assertGreater(peak(30), peak(0) + 2.0)
        self.assertEqual(float(self.table[63, :, :3].sum()), 0.0)

    def test_primary_rainbow_dispersion(self):
        angles = np.linspace(120, 180, WIDTH)
        region = (angles > 135) & (angles < 144)
        peaks = [angles[region][self.table[66, region, c].argmax()] for c in range(3)]
        self.assertTrue(136.0 < peaks[0] < peaks[1] < peaks[2] < 143.0, peaks)

    def test_glory_has_structure(self):
        angles = np.linspace(120, 180, WIDTH)
        a = self.table[64, angles > 174, :3]
        # A spectrally changing ring, rather than one neutral Gaussian glow.
        self.assertGreater(np.max(a) - np.min(a), 0.01)
        self.assertGreater(np.max(np.abs(a[:, 0] - a[:, 2])), 0.005)
        self.assertFalse(np.all(np.diff(a[:, 1]) >= 0))

    def test_solar_filter_preserves_constant_radiance(self):
        angles = np.linspace(0, np.pi, 512)
        a = np.ones((512, 3)) * 0.1
        np.testing.assert_allclose(solar_filter(a, angles), a, rtol=1e-12)


if __name__ == "__main__":
    unittest.main()
