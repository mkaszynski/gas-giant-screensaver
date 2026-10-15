#!/usr/bin/env python3
"""Measure native submissions on an isolated headless output, not scanout FPS."""

import argparse
import collections
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import tempfile
import time


def submission_report(log):
    surfaces = collections.defaultdict(list)
    for stamp, surface in re.findall(
        r"\[([\d.]+)\].*-> wl_surface[#@](\d+)\.commit\(\)", log
    ):
        surfaces[surface].append(float(stamp))
    if not surfaces:
        raise ValueError("No native frame submissions in measurement window")
    stamps = max(surfaces.values(), key=len)
    intervals = [b - a for a, b in zip(stamps, stamps[1:])]
    if len(intervals) < 2 or any(v <= 0 for v in intervals):
        raise ValueError("Insufficient or non-monotonic native submissions")
    ordered = sorted(intervals)
    return {
        "submissions": len(stamps),
        "submission_fps": 1000 * len(intervals) / (stamps[-1] - stamps[0]),
        "median_ms": statistics.median(intervals),
        "p95_ms": ordered[(len(ordered) - 1) * 95 // 100],
        "max_ms": max(intervals),
        "intervals_ms": intervals,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--data-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=20)
    parser.add_argument("--warmup", type=float, default=5)
    parser.add_argument("--refresh", type=int, default=60)
    args = parser.parse_args()
    if not 5 <= args.duration <= 600 or not 1 <= args.warmup <= 60:
        parser.error("duration must be 5..600 seconds; warmup 1..60")
    if not 30 <= args.refresh <= 240:
        parser.error("refresh must be 30..240 Hz")
    root = Path(__file__).resolve().parents[1]
    env = {k: v for k, v in os.environ.items() if k != "LD_LIBRARY_PATH"}
    # Preserve the renderer donor connection through an absolute socket path,
    # while keeping the child compositor's IPC/surfaces in a private runtime.
    if not env.get("WAYLAND_DISPLAY") or not env.get("XDG_RUNTIME_DIR"):
        parser.error("a running Wayland parent is required for the headless renderer")
    parent_socket = Path(env["WAYLAND_DISPLAY"])
    if not parent_socket.is_absolute():
        parent_socket = Path(env["XDG_RUNTIME_DIR"]) / parent_socket
    if not parent_socket.is_socket():
        parser.error("the inherited Wayland parent socket is unavailable")
    env["WAYLAND_DISPLAY"] = str(parent_socket)
    env.pop("DISPLAY", None)
    env.pop("HYPRLAND_INSTANCE_SIGNATURE", None)
    lock = compositor = None
    instance = None
    with tempfile.TemporaryDirectory(prefix="observatory-cadence-") as directory:
        tmp = Path(directory)
        # All child IPC and lock surfaces belong to this private instance.
        # The inherited parent supplies the renderer, not the measured output.
        env["XDG_RUNTIME_DIR"] = str(tmp)
        config = tmp / "hyprland.conf"
        config.write_text(
            f"monitor = ,1920x1080@{args.refresh},auto,1\n"
            "misc {\n disable_hyprland_logo = true\n"
            " disable_splash_rendering = true\n}\n"
            "animations {\n enabled = false\n}\n"
        )
        with (tmp / "compositor.log").open("wb") as compositor_log, (
            tmp / "client.log"
        ).open("wb") as client_log:
            try:
                compositor = subprocess.Popen(
                    ["Hyprland", "--config", str(config)],
                    env=env,
                    stdout=compositor_log,
                    stderr=compositor_log,
                    start_new_session=True,
                )
                for _ in range(40):
                    try:
                        instances = json.loads(
                            subprocess.check_output(
                                ["hyprctl", "instances", "-j"], env=env
                            )
                        )
                    except json.JSONDecodeError:
                        # With a fresh private runtime directory, hyprctl can
                        # report no instances before the compositor registers.
                        instances = []
                    instance = next(
                        (i for i in instances if i["pid"] == compositor.pid), None
                    )
                    if instance:
                        break
                    if compositor.poll() is not None:
                        raise RuntimeError(
                            "Isolated compositor exited during startup: "
                            + (tmp / "compositor.log").read_text(errors="replace")[
                                -2000:
                            ]
                        )
                    time.sleep(0.25)
                if not instance:
                    raise RuntimeError("Isolated compositor did not register")
                env["WAYLAND_DISPLAY"] = instance["wl_socket"]
                env["HYPRLAND_INSTANCE_SIGNATURE"] = instance["instance"]

                def control(*command):
                    return subprocess.check_output(
                        ["hyprctl", "-i", instance["instance"], *command], env=env
                    )

                name = "OBSERVATORY-CADENCE"
                control("output", "create", "headless", name)
                monitors = json.loads(control("monitors", "-j"))
                for monitor in monitors:
                    if monitor["name"] != name:
                        control("keyword", "monitor", monitor["name"] + ",disable")
                monitors = json.loads(control("monitors", "-j"))
                if len(monitors) != 1 or monitors[0]["name"] != name:
                    raise RuntimeError("Expected exactly the owned headless output")
                geometry = {
                    k: monitors[0][k] for k in ("width", "height", "refreshRate")
                }
                if geometry["width"] != 1920 or geometry["height"] != 1080:
                    raise RuntimeError("Native benchmark requires 1920x1080")
                if abs(geometry["refreshRate"] - args.refresh) > 0.1:
                    raise RuntimeError(
                        "Compositor did not select the requested refresh rate"
                    )
                native_env = dict(
                    env,
                    WAYLAND_DEBUG="client",
                    OBSERVATORY_DATA_DIR=str(args.data_dir.resolve()),
                )
                lock = subprocess.Popen(
                    [
                        str(args.package.resolve() / "bin/hyprlock"),
                        "--config",
                        str(root / "hyprlock/observatory.conf"),
                    ],
                    env=native_env,
                    stdout=client_log,
                    stderr=client_log,
                )
                time.sleep(args.warmup)
                if lock.poll() is not None:
                    raise RuntimeError("Native renderer exited during warmup")
                first = (tmp / "client.log").stat().st_size
                time.sleep(args.duration)
                last = (tmp / "client.log").stat().st_size
                if lock.poll() is not None:
                    raise RuntimeError("Native renderer exited during measurement")
                raw = (tmp / "client.log").read_bytes()
                if b"using opaque fallback" in raw:
                    raise RuntimeError(
                        "Native observatory fell back to an opaque rectangle"
                    )
                report = submission_report(raw[first:last].decode(errors="replace"))
                report.update(
                    {
                        "schema": "observatory-native-cadence-v1",
                        "geometry": geometry,
                        "duration_seconds": args.duration,
                        "warmup_seconds": args.warmup,
                        "presentation_measured": False,
                        "uses_running_wayland_parent": True,
                        "package": str(args.package.resolve()),
                    }
                )
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(report, indent=2) + "\n")
                print(
                    json.dumps({k: v for k, v in report.items() if k != "intervals_ms"})
                )
                lock.send_signal(signal.SIGUSR1)
                if lock.wait(timeout=10) != 0:
                    raise RuntimeError("Owned lock did not exit cleanly")
            finally:
                if lock and lock.poll() is None:
                    lock.kill()
                    lock.wait()
                if instance and compositor and compositor.poll() is None:
                    subprocess.run(
                        ["hyprctl", "-i", instance["instance"], "dispatch", "exit"],
                        env=env,
                        stdout=compositor_log,
                        stderr=compositor_log,
                    )
                if compositor:
                    try:
                        compositor.wait(timeout=4)
                    except subprocess.TimeoutExpired:
                        os.killpg(compositor.pid, signal.SIGKILL)
                        compositor.wait()


if __name__ == "__main__":
    main()
