"""Read-only Linux workload sampling for renderer diagnostics.

Discover GPU descriptors before/after each run. During rendering, reuse those
handles and discover new processes. Callers must reject changed client sets and
excessive sample latency. This is sampled telemetry, not an exhaustive GPU trace:
a transient new descriptor in an existing process can escape boundary discovery.
"""

import os
from pathlib import Path
import time


def active_cpu_ticks(text):
    """USER_HZ busy time, including interrupt/steal work, without idle/iowait.

    Columns: https://docs.kernel.org/filesystems/proc.html#miscellaneous-kernel-statistics-in-proc-stat
    Guest counters are not added a second time. Avoid deriving busy time from
    idle/iowait deltas: Linux documents that iowait can decrease.
    """
    fields = text.splitlines()[0].split()
    if not fields or fields[0] != "cpu" or len(fields) < 9:
        raise ValueError("Missing aggregate CPU counters")
    values = [int(v) for v in fields[1:9]]
    if any(v < 0 for v in values):
        raise ValueError("Negative aggregate CPU counter")
    return sum(values[i] for i in (0, 1, 2, 5, 6, 7))


def process_ticks(text):
    """Thread-group user/system ticks and start identity from /proc/PID/stat."""
    fields = text.rsplit(")", 1)[1].split()
    return int(fields[11]) + int(fields[12]), int(fields[19])


def gpu_info(text):
    values = dict(line.split(":", 1) for line in text.splitlines() if ":" in line)
    keys = [k for k in values if k.startswith(("drm-engine-gfx", "drm-engine-compute"))]
    if not keys or "drm-client-id" not in values:
        return None
    device = values.get("drm-pdev", values.get("drm-driver", "unknown")).strip()
    client = device + ":" + values["drm-client-id"].strip()
    return client, sum(int(values[k].split()[0]) for k in keys) / 1e9


class WorkloadSampler:
    def __init__(
        self, compositor_pid, proc_root=Path("/proc"), own_pid=None,
        ticks=None, hardware_state=lambda: {},
    ):
        self.proc = Path(proc_root)
        self.compositor = str(compositor_pid)
        self.own = str(os.getpid() if own_pid is None else own_pid)
        self.ticks = os.sysconf("SC_CLK_TCK") if ticks is None else ticks
        self.hardware_state = hardware_state
        self.handles = {}
        self.known = set()
        self.targets = {}
        self.excluded_ticks = 0

    def _cpu(self, pid):
        return process_ticks((self.proc / str(pid) / "stat").read_text())

    def _discover(self, pid):
        process = self.proc / pid
        name = (process / "comm").read_text().strip().lstrip(".")
        if name.startswith(("hyprlock", "gas-giant-scr")):
            raise RuntimeError("Competing observatory process " + pid)
        handles = []
        seen = set()
        try:
            for fd in (process / "fdinfo").iterdir():
                try:
                    info = gpu_info(fd.read_text())
                    if info and info[0] not in seen:
                        handles.append((fd, info[0]))
                        seen.add(info[0])
                except (OSError, KeyError):
                    continue
        except (PermissionError, FileNotFoundError, ProcessLookupError):
            pass
        self.handles[pid] = handles
        self.known.add(pid)

    def snapshot(self, exclude=None, refresh=False):
        start = time.monotonic()
        busy = active_cpu_ticks((self.proc / "stat").read_text())
        own, _ = self._cpu(self.own)
        compositor, _ = self._cpu(self.compositor)
        if exclude is not None:
            pid = str(exclude)
            try:
                current, identity = self._cpu(pid)
                previous, old_identity = self.targets.get(pid, (0, identity))
                increment = current if identity != old_identity else max(0, current - previous)
                self.excluded_ticks += increment
                self.targets[pid] = current, identity
            except (FileNotFoundError, ProcessLookupError):
                pass
        # Keep the last observed target CPU contribution after it exits. Any
        # unobserved tail remains background work, conservatively increasing
        # the load estimate rather than hiding another process's work.
        background = (busy - own - compositor - self.excluded_ticks) / self.ticks
        cpu = {"aggregate": background}
        engines, compositor_engines, seen = {}, {}, set()
        # Only names are needed for membership and new-process discovery.
        # Path.iterdir creates/parses a Path for every host process on every
        # sample, even when all GPU descriptors are already cached.
        pids = {name for name in os.listdir(self.proc) if name.isdigit()}
        for pid in sorted(pids, key=lambda p: p != self.compositor):
            if pid in {self.own, str(exclude)}:
                continue
            if refresh or pid not in self.known:
                try:
                    self._discover(pid)
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    continue
            for fd, expected in self.handles.get(pid, []):
                if expected in seen:
                    continue
                try:
                    info = gpu_info(fd.read_text())
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    continue
                if info is None:
                    continue
                client, seconds = info
                if client not in seen:
                    seen.add(client)
                    target = compositor_engines if pid == self.compositor else engines
                    target[client] = seconds
        self.known.intersection_update(pids)
        self.handles = {
            pid: handles for pid, handles in self.handles.items() if pid in pids
        }
        hardware = self.hardware_state()
        end = time.monotonic()
        return {
            "monotonic_seconds": (start + end) / 2,
            "read_ms": (end - start) * 1000,
            "cpu": cpu,
            "engines": engines,
            "compositor_cpu": {self.compositor: compositor / self.ticks},
            "compositor_engines": compositor_engines,
            "hardware_state": hardware,
        }
