import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('browser_budget', ROOT/'scripts/browser_budget.py')
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


class Budget(unittest.TestCase):
    def test_available_memory_uses_reclaimable_budget(self):
        self.assertEqual(budget.available_kib('MemFree: 2 kB\nMemAvailable: 12345 kB\nCached: 77 kB\n'),12345)

    def test_no_unsafe_fallback_when_budget_cannot_fit(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory)/'launched'
            result = subprocess.run(['python3',str(ROOT/'scripts/browser_budget.py'),'--report',directory+'/report.json',
                '--memory-mib','999999999','--','python3','-c',f'open({str(marker)!r},"w").close()'],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('Insufficient available memory',result.stderr)
            self.assertFalse(marker.exists())

    def test_soft_threshold_cannot_exceed_the_hard_ceiling(self):
        with tempfile.TemporaryDirectory() as directory:
            for percent in (0, 101):
                result = subprocess.run(['python3', str(ROOT/'scripts/browser_budget.py'),
                    '--report', directory+'/report.json', '--memory-high-percent', str(percent),
                    '--', 'python3', '-c', 'raise RuntimeError("must never launch")'],
                    capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('between 1 and 100', result.stderr)


if __name__ == '__main__':
    unittest.main()
