import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('browser_budget', ROOT/'scripts/browser_budget.py')
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


class Budget(unittest.TestCase):
    def launch(self, delegate=False, status=0):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / 'report.json'
            command = ['browser_budget', '--report', str(report)]
            if delegate:
                command.append('--delegate-cpu')
            command += ['--', 'python3', '-c', 'pass']
            process = MagicMock()
            process.poll.return_value = status
            process.wait.return_value = status
            original = Path.read_text

            def read(path, *args, **kwargs):
                if path == Path('/proc/meminfo'):
                    return 'MemAvailable: 99999999 kB\n'
                return original(path, *args, **kwargs)

            with patch('sys.argv', command), patch.object(budget.os, 'geteuid', return_value=1000), \
                 patch.object(Path, 'read_text', read), \
                 patch.object(budget.subprocess, 'Popen', return_value=process) as launch, \
                 patch.object(budget.subprocess, 'run') as run:
                run.return_value.stdout = 'Result=success\nExecMainStatus=0\n'
                result = budget.main()
            return result, launch.call_args.args[0], json.loads(report.read_text()), launch.call_count

    def test_delegation_preserves_every_parent_resource_limit(self):
        for delegate in (False, True):
            with self.subTest(delegate=delegate):
                result, command, report, launches = self.launch(delegate)
                self.assertEqual(result, 0)
                self.assertEqual(launches, 1)
                self.assertEqual('Delegate=cpu' in command, delegate)
                for limit in ('MemoryMax=1100M', 'MemoryHigh=902M',
                              'MemorySwapMax=0', 'CPUQuota=90%',
                              'TasksMax=192', 'RuntimeMaxSec=1200', 'OOMPolicy=kill'):
                    self.assertIn(limit, command)
                self.assertEqual(report['delegate_cpu'], delegate)
                self.assertEqual(report['cpu_percent'], 90)
                self.assertEqual(report['memory_mib'], 1100)

    def test_failed_delegated_launch_has_no_unbounded_retry(self):
        result, command, report, launches = self.launch(True, 1)
        self.assertEqual(result, 1)
        self.assertEqual(report['exit'], 1)
        self.assertEqual(launches, 1)
        self.assertIn('Delegate=cpu', command)

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
