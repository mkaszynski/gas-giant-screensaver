#!/usr/bin/env python3
"""Paired 1080p weather/landscape render measurements; never changes GPU clocks."""

import argparse
import csv
import hashlib
import json
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CASES = [
    ("forest-cloudbanks", 510, 162000, 1790900000),
    ("forest-cumulus", 510, 162000, 0),
    ("forest-storm", 510, 162000, 1260),
    ("ocean", 1740, 0, 0),
    ("snow", 360, 210600, 1790900000),
    ("coast", 1740, 5600.390625, 1790900000),
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summary(rows):
    values = sorted(float(row["render_ms"]) for row in rows)
    gpu = [float(row["gpu_ms"]) for row in rows if row["gpu_ms"]]
    return {
        "frames": len(values),
        "median_ms": statistics.median(values),
        "p95_ms": values[(len(values) - 1) * 95 // 100],
        "p99_ms": values[(len(values) - 1) * 99 // 100],
        "max_ms": max(values),
        "deadline_misses": sum(value > 1000 / 30 for value in values),
        "gpu_median_ms": statistics.median(gpu) if gpu else None,
    }


def display_state():
    try:
        monitors = json.loads(subprocess.check_output(["hyprctl", "monitors", "-j"]))
        # Geometry and power only: do not publish EDID serials or workspace data.
        return [
            {key: m[key] for key in ("width", "height", "refreshRate", "dpmsStatus")}
            for m in monitors
        ]
    except (OSError, subprocess.SubprocessError, ValueError, KeyError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--baseline", type=Path, required=True, help="Nix package directory"
    )
    parser.add_argument(
        "--candidate", type=Path, default=ROOT / "build/gas-giant-screensaver"
    )
    parser.add_argument("--data-dir", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames-dir", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=3)
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--skip", type=int, default=10)
    args = parser.parse_args()
    if args.pairs < 1 or args.skip < 0 or args.frames <= args.skip:
        parser.error("need positive pairs and more frames than skipped frames")
    baseline = args.baseline.resolve()
    candidate = args.candidate.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.frames_dir.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": "observatory-weather-benchmark-v1",
        "completed": False,
        "checkout_source_sha256": {
            str(p.relative_to(ROOT)): digest(p)
            for p in sorted((ROOT / "src").rglob("*"))
            if p.suffix in (".hpp", ".cpp")
        },
        "resolution": [1920, 1080],
        "target_fps": 30,
        "frames_per_run": args.frames,
        "startup_frames_excluded_from_steady": args.skip,
        "timings_exclude_presentation": True,
        "display_state_before": display_state(),
        "baseline_binary_sha256": digest(baseline / "bin/gas-giant-screensaver"),
        "candidate_binary_sha256": digest(candidate),
        "candidate_shader_sha256": {
            p.name: digest(p)
            for p in sorted((args.data_dir / "shaders").iterdir())
            if p.is_file()
        },
        "runs": [],
    }
    for pair in range(args.pairs):
        for name, sky, route, weather in CASES:
            order = (
                ("baseline", "candidate")
                if pair % 2 == 0
                else ("candidate", "baseline")
            )
            for version in order:
                binary = (
                    baseline / "bin/gas-giant-screensaver"
                    if version == "baseline"
                    else candidate
                )
                data = (
                    baseline / "share/gas-giant-screensaver"
                    if version == "baseline"
                    else args.data_dir
                )
                frames = args.frames_dir / f"{name}-{pair + 1}-{version}.csv"
                run = subprocess.run(
                    [
                        str(binary),
                        "--data-dir",
                        str(data),
                        "--benchmark",
                        str(args.frames),
                        "--fps",
                        "30",
                        "--width",
                        "1920",
                        "--height",
                        "1080",
                        "--time",
                        str(sky),
                        "--travel-time",
                        str(route),
                        "--weather-time",
                        str(weather),
                        "--benchmark-output",
                        str(frames),
                    ],
                    text=True,
                    capture_output=True,
                    check=True,
                    cwd=ROOT,
                )
                if (
                    version == "candidate"
                    and "Cloud buffer: 1920x1080" not in run.stdout
                ):
                    raise RuntimeError(
                        "Candidate did not verify full-resolution cloud buffers"
                    )
                rows = list(csv.DictReader(frames.open()))
                if len(rows) != args.frames:
                    raise RuntimeError("Incomplete benchmark frame records")
                entry = {
                    "scene": name,
                    "pair": pair + 1,
                    "version": version,
                    "sky_seconds": sky,
                    "route_seconds": route,
                    "weather_seconds": weather,
                    "all_frames": summary(rows),
                    "steady": summary(rows[args.skip :]),
                }
                report["runs"].append(entry)
                print(json.dumps(entry), flush=True)
                args.output.write_text(json.dumps(report, indent=2) + "\n")
    if digest(candidate) != report["candidate_binary_sha256"]:
        raise RuntimeError("Candidate binary changed during measurements")
    for name, expected in report["candidate_shader_sha256"].items():
        if digest(args.data_dir / "shaders" / name) != expected:
            raise RuntimeError("Candidate shaders changed during measurements")
    for name, expected in report["checkout_source_sha256"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError("Checkout source changed during measurements")
    report["completed"] = True
    report["display_state_after"] = display_state()
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
