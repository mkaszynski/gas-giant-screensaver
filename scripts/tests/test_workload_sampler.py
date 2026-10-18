import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "workload_sampler.py"
spec = importlib.util.spec_from_file_location("workload_sampler", SOURCE)
sampler_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sampler_module)


class WorkloadSamplerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.proc = Path(self.temp.name)
        self.host()
        self.process(999, "collector", 100)
        self.process(100, "Hyprland", 200)
        self.process(101, "browser", 50)
        self.fd(100, 10)
        self.fd(101, 11)
        self.sampler = sampler_module.WorkloadSampler(100, self.proc, own_pid=999, ticks=100)

    def host(self, user=10000):
        (self.proc / "stat").write_text(f"cpu {user} 200 1000 9000 300 30 40 10 999 888\n")

    def process(self, pid, name, ticks, identity=1000):
        proc = self.proc / str(pid)
        (proc / "fdinfo").mkdir(parents=True, exist_ok=True)
        fields = ["0"] * 24
        fields[0], fields[11], fields[19] = "S", str(ticks), str(identity)
        (proc / "stat").write_text(f"{pid} ({name}) " + " ".join(fields))
        (proc / "comm").write_text(name)

    def fd(self, pid, client, gfx=2000000, compute=3000000, fd=7, device="0000:01:00.0"):
        (self.proc / str(pid) / "fdinfo" / str(fd)).write_text(
            f"drm-pdev: {device}\ndrm-client-id: {client}\ndrm-engine-gfx: {gfx} ns\ndrm-engine-compute: {compute} ns\n")

    def test_aggregate_cpu_avoids_idle_iowait_and_guest_duplication(self):
        self.assertEqual(sampler_module.active_cpu_ticks((self.proc / "stat").read_text()), 11280)
        with self.assertRaises(ValueError):
            sampler_module.active_cpu_ticks("cpu 1 2\n")
        with self.assertRaises(ValueError):
            sampler_module.active_cpu_ticks("cpu -1 2 3 4 5 6 7 8\n")

    def test_stat_names_may_contain_spaces_and_parentheses(self):
        self.process(102, "weird ) ( process", 123, identity=456)
        self.assertEqual(sampler_module.process_ticks((self.proc / "102/stat").read_text()), (123, 456))

    def test_cached_samples_do_not_reread_all_process_metadata(self):
        self.sampler.snapshot(refresh=True)
        reads = []
        original = Path.read_text
        def track(path, *args, **kwargs):
            reads.append(path)
            return original(path, *args, **kwargs)
        with patch.object(Path, "read_text", track):
            self.sampler.snapshot()
        self.assertNotIn(self.proc / "101/stat", reads)
        self.assertNotIn(self.proc / "101/comm", reads)
        self.assertIn(self.proc / "stat", reads)
        self.assertIn(self.proc / "999/stat", reads)

    def test_duplicate_handles_read_and_count_one_client(self):
        self.fd(101, 11, fd=8)
        first = self.sampler.snapshot(refresh=True)
        self.assertEqual(len(self.sampler.handles["101"]), 1)
        self.assertEqual(first["engines"], {"0000:01:00.0:11": .005})
        self.fd(101, 11, gfx=4000000)
        self.fd(101, 11, gfx=4000000, fd=8)
        self.assertEqual(self.sampler.snapshot()["engines"], {"0000:01:00.0:11": .007})

    def test_distinct_devices_do_not_alias_client_ids(self):
        self.fd(101, 11, device="0000:02:00.0", fd=8)
        self.assertEqual(len(self.sampler.snapshot(refresh=True)["engines"]), 2)

    def test_compositor_accounted_separately(self):
        state = self.sampler.snapshot(refresh=True)
        self.assertEqual(state["compositor_engines"], {"0000:01:00.0:10": .005})
        self.assertEqual(state["compositor_cpu"], {"100": 2.0})
        self.assertEqual(state["cpu"], {"aggregate": 109.8})

    def test_new_client_process_is_discovered_during_cached_samples(self):
        self.sampler.snapshot(refresh=True)
        self.process(102, "new-client", 10)
        self.fd(102, 12)
        self.assertIn("0000:01:00.0:12", self.sampler.snapshot()["engines"])

    def test_large_cached_process_set_keeps_metadata_reads_bounded(self):
        # A busy desktop can have hundreds of non-GPU processes. Cached
        # samples must still discover a new GPU client without rereading
        # metadata for all of those already-known processes.
        for pid in range(2000, 3000):
            self.process(pid, "worker", 0)
        (self.proc / "non-process-entry").mkdir()
        self.sampler.snapshot(refresh=True)
        self.process(3000, "new-client", 10)
        self.fd(3000, 12)
        reads = []
        original = Path.read_text

        def track(path, *args, **kwargs):
            reads.append(path)
            return original(path, *args, **kwargs)

        with patch.object(Path, "read_text", track):
            state = self.sampler.snapshot()
        self.assertIn("0000:01:00.0:12", state["engines"])
        self.assertLessEqual(len(reads), 8)
        self.assertNotIn("non-process-entry", self.sampler.known)

    def test_new_handle_on_existing_process_is_found_at_boundary(self):
        before = self.sampler.snapshot(refresh=True)
        self.fd(101, 12, fd=8)
        after = self.sampler.snapshot(refresh=True)
        self.assertNotEqual(set(before["engines"]), set(after["engines"]))

    def test_vanished_handle_exposes_changed_coverage(self):
        before = self.sampler.snapshot(refresh=True)
        (self.proc / "101/fdinfo/7").unlink()
        self.assertNotEqual(set(before["engines"]), set(self.sampler.snapshot()["engines"]))

    def test_cpu_includes_transient_work_and_retains_target_exclusion_on_exit(self):
        before = self.sampler.snapshot(refresh=True)
        self.process(102, "gas-giant-scree", 25)
        self.host(10100)
        self.process(999, "collector", 105)
        self.process(100, "Hyprland", 215)
        during = self.sampler.snapshot(exclude=102)
        self.assertAlmostEqual(during["cpu"]["aggregate"] - before["cpu"]["aggregate"], .55)
        # Target exits. Its unseen CPU tail is conservatively counted as load.
        (self.proc / "102/stat").unlink()
        (self.proc / "102/comm").unlink()
        (self.proc / "102/fdinfo").rmdir()
        (self.proc / "102").rmdir()
        self.host(10130)
        self.process(999, "collector", 110)
        self.process(100, "Hyprland", 220)
        after = self.sampler.snapshot(refresh=True)
        self.assertAlmostEqual(after["cpu"]["aggregate"] - during["cpu"]["aggregate"], .2)

    def test_reused_target_pid_has_new_cpu_identity(self):
        self.process(102, "gas-giant-scree", 20, identity=500)
        self.sampler.snapshot(exclude=102)
        self.process(102, "gas-giant-scree", 30, identity=600)
        self.sampler.snapshot(exclude=102)
        self.assertEqual(self.sampler.excluded_ticks, 50)

    def test_new_lock_wrapper_rejected(self):
        self.sampler.snapshot(refresh=True)
        self.process(102, ".hyprlock-wrapp", 0)
        with self.assertRaisesRegex(RuntimeError, "Competing observatory"):
            self.sampler.snapshot()

    def test_boundary_rechecks_existing_process_names(self):
        self.sampler.snapshot(refresh=True)
        (self.proc / "101/comm").write_text(".gas-giant-scre")
        with self.assertRaisesRegex(RuntimeError, "Competing observatory"):
            self.sampler.snapshot(refresh=True)

    def test_hardware_reads_are_included_in_overhead(self):
        self.sampler.hardware_state = lambda: {"temperature": 1}
        with patch.object(sampler_module.time, "monotonic", side_effect=[1., 1.015]):
            result = self.sampler.snapshot(refresh=True)
        self.assertAlmostEqual(result["read_ms"], 15)
        self.assertEqual(result["hardware_state"], {"temperature": 1})


if __name__ == "__main__":
    unittest.main()
