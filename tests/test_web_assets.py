import importlib.util
from pathlib import Path
import tempfile
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('web_assets', ROOT / 'scripts/web_assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class Assets(unittest.TestCase):
    def test_groups_are_disjoint_and_minimal_is_small(self):
        groups = assets.manifest()
        files = [file for paths in groups.values() for file in paths]
        self.assertEqual(len(files), len(set(files)))
        self.assertLess(sum((ROOT / p).stat().st_size for g in ('base', 'terrain') for p in groups[g]), 8*1024*1024)
        self.assertIn('assets/world/flight.ggflight', groups['world'])
        self.assertIn('assets/world/landforms/arid.ggrelief', groups['world'])
        self.assertIn('assets/world/optics/phase.ggoptics', groups['world'])
        self.assertEqual(len(groups['galaxy']), 6)
        self.assertEqual(groups['stars'], ['assets/sky/stars.bin'])

    def test_staged_files_retain_native_bytes_and_credits(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run(['python3', str(ROOT / 'scripts/web_assets.py'), '--output', directory], check=True)
            target = Path(directory)
            self.assertEqual((target/'assets/giant.png').read_bytes(), (ROOT/'assets/giant.png').read_bytes())
            optics = Path('assets/world/optics/phase.ggoptics')
            self.assertEqual((target/optics).read_bytes(), (ROOT/optics).read_bytes())
            self.assertTrue((target/'assets/sky/ATTRIBUTION.md').is_file())


if __name__ == '__main__':
    unittest.main()
