"""Native cadence parsing is independent of machine paths and compositor IDs."""

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "native_benchmark",
    Path(__file__).resolve().parents[1] / "scripts/benchmark_native.py",
)
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


class NativeCadenceTests(unittest.TestCase):
    def test_uses_commits_not_frame_requests(self):
        log = "\n".join(
            f"[{100 + n * 33.333:.3f}] {{mesa egl surface queue}} -> wl_surface#21.commit()"
            for n in range(10)
        )
        log += "\n[102.0] -> wl_surface#23.commit()\n"
        log += "\n[104.0] -> wl_surface#21.frame(new id wl_callback#9)\n"
        result = native.submission_report(log)
        self.assertEqual(result["submissions"], 10)
        self.assertAlmostEqual(result["submission_fps"], 30, places=3)

    def test_accepts_legacy_object_delimiter(self):
        log = "\n".join(f"[{t}] -> wl_surface@9.commit()" for t in [0, 33, 66])
        self.assertEqual(native.submission_report(log)["submissions"], 3)

    def test_accepts_wayland_padded_timestamps(self):
        log = "\n".join(
            f"[{t:11.3f}] {{mesa egl surface queue}} -> wl_surface#21.commit()"
            for t in [749400.077, 749433.41, 749466.743]
        )
        self.assertEqual(native.submission_report(log)["submissions"], 3)
        self.assertAlmostEqual(
            native.submission_report(log)["submission_fps"], 30, places=3
        )

    def test_retains_stalls(self):
        log = "\n".join(f"[{t}] -> wl_surface#9.commit()" for t in [0, 33, 66, 130])
        result = native.submission_report(log)
        self.assertEqual(result["max_ms"], 64)
        self.assertEqual(result["intervals_ms"], [33, 33, 64])

    def test_rejects_missing_frames(self):
        with self.assertRaises(ValueError):
            native.submission_report("[3] -> wl_surface#9.frame()")

    def test_rejects_nonmonotonic_timestamps(self):
        with self.assertRaises(ValueError):
            native.submission_report(
                "\n".join(f"[{t}] -> wl_surface#9.commit()" for t in [50, 40, 30])
            )


if __name__ == "__main__":
    unittest.main()
