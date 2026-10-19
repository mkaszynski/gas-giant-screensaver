import json
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/summarize_web_memory.py'


class MemorySummary(unittest.TestCase):
    def run_summary(self, **changes):
        with tempfile.TemporaryDirectory() as directory:
            captures = Path(directory) / 'captures'
            captures.mkdir()
            for backend in ('webgl2', 'webgpu'):
                for version in ('before', 'after'):
                    names = ['minimal', 'stars', 'galaxy', 'terrain', 'clouds', 'complete']
                    if version == 'after':
                        names.append('auto-low')
                    stages = []
                    for name in names:
                        stage = dict(name=name, assetBytes=0, heap=64*1024*1024,
                                     resources={'bytes': 32*1024*1024}, assetDownloaded=1024,
                                     sampledPeak=512*1024*1024, cpuMedianMs=5,
                                     completionP95Ms=66, completedFps=30,
                                     maxInFlight=2 if version == 'after' else 1)
                        if version == 'after':
                            stage.update(changes)
                        stages.append(stage)
                    (captures / f'web-memory-{version}-{backend}.json').write_text(
                        json.dumps({'stages': stages}))
            return subprocess.run(['python3', str(SCRIPT)], cwd=directory,
                                  capture_output=True, text=True)

    def test_reports_throughput_separately_from_queued_latency(self):
        result = self.run_summary()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Queue completion p95 ms | Completed fps | Max queued', result.stdout)
        self.assertIn('66.0 | 30.00 | 2', result.stdout)

    def test_rejects_unbounded_submission_queue(self):
        result = self.run_summary(maxInFlight=3)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('GPU queue exceeded', result.stderr)

    def test_requires_measured_positive_finite_throughput(self):
        for fps in (0, float('inf'), float('nan')):
            with self.subTest(fps=fps):
                result = self.run_summary(completedFps=fps)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Completed throughput must be measured', result.stderr)


if __name__ == '__main__':
    unittest.main()
