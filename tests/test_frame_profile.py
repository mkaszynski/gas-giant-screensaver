"""Exercise frame/telemetry correlation without requiring a GPU or sysfs."""

import importlib.util
from pathlib import Path
import tempfile
import csv
import json
import subprocess
import sys
import unittest

spec = importlib.util.spec_from_file_location(
    "profile_frame_stalls",
    Path(__file__).resolve().parents[1] / "scripts/profile_frame_stalls.py",
)
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class FrameProfileTests(unittest.TestCase):
    def frames(self):
        return [
            {
                "frame": str(i),
                "render_ms": "20",
                "submit_ms": "2",
                "wait_ms": "18",
                "gpu_ms": "19",
                "monotonic_seconds": str(i),
                "mesh_build_pending": "0",
            }
            for i in range(15)
        ]

    def test_retains_stalls_and_worker_context(self):
        frames = self.frames()
        frames[13].update(render_ms="50", wait_ms="48", mesh_build_pending="1")
        result = profile.analyze(frames, [])
        self.assertEqual(result["steady_frames"], 5)
        self.assertEqual(result["deadline_misses"], 1)
        self.assertEqual(result["render_ms"]["maximum"], 50)
        self.assertEqual(result["worst_frames"][0]["frame"]["mesh_build_pending"], "1")

    def test_bounds_sparse_sensor_uncertainty(self):
        samples = [{"monotonic_seconds": 9.99}, {"monotonic_seconds": 10.04}]
        result = profile.analyze(self.frames(), samples)
        observation = next(
            r for r in result["worst_frames"] if r["frame"]["frame"] == "10"
        )
        self.assertEqual(observation["inside_samples"], [])
        self.assertEqual(observation["preceding_sample"], samples[0])
        self.assertEqual(observation["following_sample"], samples[1])

    def test_assigns_samples_to_actual_render_interval(self):
        sample = {"monotonic_seconds": 10.01, "gpu_clock_hz": 400000000}
        result = profile.analyze(self.frames(), [sample])
        observation = next(
            r for r in result["worst_frames"] if r["frame"]["frame"] == "10"
        )
        self.assertEqual(observation["inside_samples"], [sample])
        self.assertIsNone(observation["following_sample"])

    def test_missing_gpu_timer_is_not_zero_cost(self):
        frames = self.frames()
        for frame in frames:
            frame["gpu_ms"] = ""
        self.assertIsNone(profile.analyze(frames, [])["gpu_ms"])

    def test_rejects_missing_steady_frames(self):
        with self.assertRaises(ValueError):
            profile.analyze(self.frames()[:10], [])

    def test_process_stat_names_and_missing_process(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stat"
            self.assertIsNone(profile.cpu_ticks(path))
            fields = ["S", *["0"] * 10, "17", "3", *["0"] * 5]
            path.write_text("123 (a (worker) name) " + " ".join(fields))
            self.assertEqual(profile.cpu_ticks(path), 20)

    def test_cli_runs_owned_binary_without_gpu_telemetry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = root / "fixture.csv"
            with rows.open("w") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(self.frames()[0]))
                writer.writeheader()
                writer.writerows(self.frames())
            fake = root / "benchmark"
            fake.write_text(
                "#!/usr/bin/env python3\n"
                "import sys,shutil\n"
                f"shutil.copyfile({str(rows)!r},sys.argv[sys.argv.index('--benchmark-output')+1])\n"
            )
            fake.chmod(0o700)
            prefix = root / "capture"
            subprocess.run(
                [
                    sys.executable,
                    str(Path(profile.__file__)),
                    "--binary",
                    str(fake),
                    "--output-prefix",
                    str(prefix),
                    "--frames",
                    "15",
                    "--no-telemetry",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = json.loads(Path(str(prefix) + ".summary.json").read_text())
            self.assertFalse(result["telemetry_enabled"])
            self.assertFalse(result["presentation_measured"])
            self.assertEqual(result["steady_frames"], 5)
            self.assertEqual(Path(str(prefix) + ".telemetry.jsonl").read_text(), "")


if __name__ == "__main__":
    unittest.main()
