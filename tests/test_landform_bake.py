"""Check coherent numerical relief, source boundaries and cache integrity."""

import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bake_landforms import extract, SOURCES


class LandformBakeTests(unittest.TestCase):
    def test_no_voids_or_out_of_tile_sampling(self):
        dem = np.zeros((3601, 3601), dtype=np.int16)
        bad = dict(SOURCES["arid"])
        bad["longitude"] = -130
        with self.assertRaises(ValueError):
            extract(dem, bad, 64)
        dem[0, 0] = -32768
        with self.assertRaises(ValueError):
            extract(dem, SOURCES["arid"], 64)
        with self.assertRaises(ValueError):
            extract(np.zeros((64, 64)), SOURCES["arid"], 64)

    def test_constant_elevation_has_no_fake_detail(self):
        dem = np.full((3601, 3601), 900, dtype=np.int16)
        for source in SOURCES.values():
            np.testing.assert_array_equal(extract(dem, source, 64), np.zeros((64, 64)))

    def test_committed_relief_and_lighting_have_matching_inputs(self):
        root = Path(__file__).resolve().parents[1] / "assets/world/landforms"
        manifest = json.loads((root / "manifest.json").read_text())
        self.assertFalse(manifest["photographic_input"])
        for name, source in SOURCES.items():
            m = manifest["sources"][name]
            self.assertEqual(source["sha256"], m["sha256"])
            data = (root / (name + ".ggrelief")).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), m["relief_sha256"])
            magic, w, h, spacing = struct.unpack("<8sIIf", data[:20])
            self.assertEqual((magic, w, h, spacing), (b"GGRELIF1", 2048, 2048, 16))
            self.assertEqual(len(data), 20 + w * h * 2)
            elevations = np.frombuffer(data[20:], "<i2")
            self.assertLess(abs(elevations.mean()), 1)
            self.assertGreater(elevations.std(), 100)
            self.assertLess(elevations.std(), 400)
            self.assertEqual(
                hashlib.sha256(
                    (root / (name + "-material.png")).read_bytes()
                ).hexdigest(),
                m["material_sha256"],
            )


if __name__ == "__main__":
    unittest.main()
