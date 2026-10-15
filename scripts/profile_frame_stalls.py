#!/usr/bin/env python3
"""Correlate synchronized renderer frames with read-only Linux GPU telemetry."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {
        "median": statistics.median(values),
        "p95": values[(len(values) - 1) * 95 // 100],
        "maximum": values[-1],
    }


def analyze(frames, samples):
    steady = frames[10:]
    if not steady:
        raise ValueError("At least eleven benchmark frames are required")
    report = {
        "steady_frames": len(steady),
        "deadline_misses": sum(float(f["render_ms"]) > 1000 / 30 for f in steady),
    }
    for field in ("render_ms", "gpu_ms", "submit_ms", "wait_ms"):
        report[field] = distribution([float(f[field]) for f in steady if f.get(field)])
    # These observations deliberately do not infer a causal clock/worker fault.
    # Neighboring samples bound the uncertainty when no sample falls in a frame.
    worst = []
    for frame in sorted(steady, key=lambda f: float(f["render_ms"]), reverse=True)[:20]:
        start = float(frame["monotonic_seconds"])
        end = start + float(frame["render_ms"]) / 1000
        before = [s for s in samples if s["monotonic_seconds"] <= start]
        after = [s for s in samples if s["monotonic_seconds"] >= end]
        inside = [s for s in samples if start < s["monotonic_seconds"] < end]
        worst.append(
            {
                "frame": frame,
                "preceding_sample": before[-1] if before else None,
                "following_sample": after[0] if after else None,
                "inside_samples": inside,
            }
        )
    report["worst_frames"] = worst
    return report


def read_number(path):
    try:
        return int(path.read_text().strip())
    except (OSError, ValueError):
        return None


def cpu_ticks(path):
    try:
        fields = path.read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])
    except (OSError, IndexError, ValueError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path, default=Path("build/gas-giant-screensaver")
    )
    parser.add_argument("--data-dir", type=Path)
    parser.add_argument("--output-prefix", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=720)
    parser.add_argument("--travel-speed", type=int, choices=(0, 1), default=1)
    parser.add_argument("--time", type=float, default=510)
    parser.add_argument("--travel-time", type=float, default=162000)
    parser.add_argument("--sample-ms", type=int, default=10)
    parser.add_argument("--gpu-device", type=Path)
    parser.add_argument("--no-telemetry", action="store_true")
    args = parser.parse_args()
    if not 11 <= args.frames <= 10000 or not 5 <= args.sample_ms <= 1000:
        parser.error("frames must be 11..10000 and sample-ms 5..1000")
    device = args.gpu_device
    if device is None and not args.no_telemetry:
        devices = list(Path("/sys/class/drm").glob("card*/device/gpu_busy_percent"))
        if len(devices) != 1:
            parser.error(
                "choose --gpu-device when exactly one readable GPU is not present"
            )
        device = devices[0].parent
    sensors = {}
    if device is not None:
        sensors["gpu_busy_percent"] = device / "gpu_busy_percent"
        for field, pattern in (
            ("gpu_clock_hz", "hwmon/hwmon*/freq1_input"),
            ("gpu_temperature_millicelsius", "hwmon/hwmon*/temp1_input"),
            ("device_power_microwatts", "hwmon/hwmon*/power1_input"),
        ):
            matches = list(device.glob(pattern))
            if len(matches) == 1:
                sensors[field] = matches[0]
    prefix = args.output_prefix
    prefix.parent.mkdir(parents=True, exist_ok=True)
    frame_path = Path(str(prefix) + ".frames.csv")
    sample_path = Path(str(prefix) + ".telemetry.jsonl")
    command = [
        str(args.binary.resolve()),
        "--benchmark",
        str(args.frames),
        "--benchmark-output",
        str(frame_path),
        "--width",
        "1920",
        "--height",
        "1080",
        "--fps",
        "30",
        "--time",
        str(args.time),
        "--travel-time",
        str(args.travel_time),
        "--travel-speed",
        str(args.travel_speed),
    ]
    if args.data_dir:
        command += ["--data-dir", str(args.data_dir.resolve())]
    env = {k: v for k, v in os.environ.items() if k != "LD_LIBRARY_PATH"}
    try:
        outputs = json.loads(
            subprocess.check_output(
                ["hyprctl", "monitors", "-j"], env=env, stderr=subprocess.DEVNULL
            )
        )
        parent_outputs = [
            {
                key: output[key]
                for key in ("width", "height", "refreshRate", "dpmsStatus")
            }
            for output in outputs
        ]
    except (OSError, ValueError, KeyError, subprocess.SubprocessError):
        parent_outputs = None
    samples = []
    sampler_start_cpu = time.process_time()
    with Path(str(prefix) + ".log").open("wb") as log, sample_path.open("w") as output:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=log)
        proc = Path("/proc") / str(process.pid)
        deadline = time.monotonic() + 60 + args.frames / 30 * 4
        try:
            while process.poll() is None:
                start = time.monotonic()
                if start > deadline:
                    raise TimeoutError("Owned benchmark exceeded its time limit")
                if not args.no_telemetry:
                    sample = {key: read_number(path) for key, path in sensors.items()}
                    sample["process_cpu_ticks"] = cpu_ticks(proc / "stat")
                    sample["main_thread_cpu_ticks"] = cpu_ticks(
                        proc / "task" / str(process.pid) / "stat"
                    )
                    finish = time.monotonic()
                    sample.update(
                        monotonic_seconds=(start + finish) / 2,
                        sample_duration_ms=(finish - start) * 1000,
                    )
                    samples.append(sample)
                    output.write(json.dumps(sample) + "\n")
                time.sleep(max(0.0, start + args.sample_ms / 1000 - time.monotonic()))
            if process.returncode != 0:
                raise RuntimeError("Benchmark failed; inspect " + str(prefix) + ".log")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    sampler_cpu_seconds = time.process_time() - sampler_start_cpu
    with frame_path.open() as stream:
        frames = list(csv.DictReader(stream))
    report = analyze(frames, samples)
    report.update(
        schema="observatory-frame-profile-v1",
        binary_sha256=hashlib.sha256(args.binary.resolve().read_bytes()).hexdigest(),
        command=[str(args.binary), *command[1:]],
        sample_interval_ms=args.sample_ms,
        telemetry_enabled=not args.no_telemetry,
        sensors={key: str(path) for key, path in sensors.items()},
        cpu_ticks_per_second=os.sysconf("SC_CLK_TCK"),
        sampler_cpu_seconds=sampler_cpu_seconds,
        sample_read_ms=distribution([s["sample_duration_ms"] for s in samples]),
        presentation_measured=False,
        parent_output_state=parent_outputs,
    )
    Path(str(prefix) + ".summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps({k: v for k, v in report.items() if k != "worst_frames"}, indent=2)
    )


if __name__ == "__main__":
    main()
