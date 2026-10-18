"""A real lock or sleeping parent invalidates native timing, including Nix wrappers."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "benchmark_native", Path(__file__).resolve().parents[1] / "benchmark_native.py"
)
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


class BenchmarkHostTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.proc = Path(self.directory.name)
        self.outputs = [{"width": 1920, "height": 1080, "dpmsStatus": True}]

    def process(self, pid, name):
        path = self.proc / str(pid)
        path.mkdir()
        (path / "comm").write_text(name + "\n")

    def check(self, excluded=()):
        native.require_benchmark_host(self.outputs, self.proc, excluded)

    def test_allows_awake_desktop_without_other_observatories(self):
        for pid, name in enumerate(["Hyprland", "python3", "nix-daemon", "factory"]):
            self.process(pid, name)
        self.check()

    def test_rejects_unwrapped_and_nix_wrapped_renderers(self):
        for name in ["hyprlock", ".hyprlock-wrapp", "gas-giant-scree", ".gas-giant-scre"]:
            with self.subTest(name=name):
                self.process(12, name)
                with self.assertRaisesRegex(ValueError, "PID 12"):
                    self.check()
                (self.proc / "12" / "comm").unlink()
                (self.proc / "12").rmdir()

    def test_excludes_only_the_owned_test_lock(self):
        self.process(12, ".hyprlock-wrapp")
        self.check(excluded={12})
        self.process(13, ".hyprlock-wrapp")
        with self.assertRaisesRegex(ValueError, "PID 13"):
            self.check(excluded={12})

    def test_rejects_sleeping_output_even_if_another_is_awake(self):
        self.outputs.append({"dpmsStatus": False})
        with self.assertRaisesRegex(ValueError, "awake"):
            self.check()

    def test_rejects_missing_or_unknown_output_state(self):
        for outputs in [None, [], [{}]]:
            with self.subTest(outputs=outputs), self.assertRaises(ValueError):
                native.require_benchmark_host(outputs, self.proc)

    def test_tolerates_process_that_exited_during_scan(self):
        (self.proc / "12").mkdir()
        self.check()

    def test_does_not_treat_unreadable_process_state_as_clean(self):
        self.process(12, "python3")
        with mock.patch.object(Path, "read_text", side_effect=PermissionError):
            with self.assertRaises(PermissionError):
                self.check()


if __name__ == "__main__":
    unittest.main()
