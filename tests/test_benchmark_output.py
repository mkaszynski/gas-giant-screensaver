"""Exercise the real GPU benchmark's machine-readable frame records."""

import csv
import math
from pathlib import Path
import subprocess
import sys
import tempfile

binary = sys.argv[1] if len(sys.argv) > 1 else "./build/gas-giant-screensaver"
with tempfile.TemporaryDirectory(prefix="observatory-benchmark-") as directory:
    output = Path(directory) / "frames.csv"
    run = subprocess.run(
        [
            binary,
            "--benchmark",
            "3",
            "--benchmark-output",
            str(output),
            "--width",
            "128",
            "--height",
            "72",
            "--time",
            "510",
            "--travel-time",
            "162000",
            "--travel-speed",
            "0",
        ],
        text=True,
        capture_output=True,
        check=True,
    )
    rows = list(csv.DictReader(output.open()))
    assert len(rows) == 3, rows
    required = {
        "frame",
        "scene_seconds",
        "render_ms",
        "gpu_ms",
        "mesh_revision",
        "shadow_revision",
        "upload_bytes",
        "reflection_revision",
        "submit_ms",
        "wait_ms",
        "monotonic_seconds",
        "mesh_builds_started",
        "mesh_build_pending",
    }
    assert set(rows[0]) == required, rows[0]
    for frame, row in enumerate(rows):
        assert int(row["frame"]) == frame
        assert math.isfinite(float(row["scene_seconds"]))
        assert math.isfinite(float(row["render_ms"])) and float(row["render_ms"]) > 0
        if row["gpu_ms"]:
            assert math.isfinite(float(row["gpu_ms"])) and float(row["gpu_ms"]) > 0
        assert int(row["mesh_revision"]) >= 1
        assert int(row["shadow_revision"]) >= 4
        submit = float(row["submit_ms"])
        wait = float(row["wait_ms"])
        assert math.isfinite(submit) and submit > 0
        assert math.isfinite(wait) and wait >= 0
        assert abs(submit + wait - float(row["render_ms"])) < 1e-6
        stamp = float(row["monotonic_seconds"])
        assert math.isfinite(stamp) and stamp > 0
        if frame:
            assert stamp > float(rows[frame - 1]["monotonic_seconds"])
        assert int(row["mesh_builds_started"]) == 1
        assert int(row["mesh_build_pending"]) == 0
        if frame == 0:
            assert int(row["upload_bytes"]) > 0
        else:
            assert int(row["upload_bytes"]) <= 2 * 1024 * 1024
    assert "p99=" in run.stdout and "deadline_misses=" in run.stdout, run.stdout
    ordinary = subprocess.run(
        [
            binary,
            "--benchmark",
            "1",
            "--width",
            "128",
            "--height",
            "72",
            "--time",
            "510",
            "--travel-time",
            "162000",
        ],
        text=True,
        capture_output=True,
        check=True,
    )
    assert "deadline_misses=" in ordinary.stdout, ordinary.stdout
    bad = subprocess.run(
        [binary, "--benchmark", "1", "--benchmark-output", str(output / "missing")],
        text=True,
        capture_output=True,
    )
    assert (
        bad.returncode != 0 and "Cannot write benchmark output" in bad.stderr
    ), bad.stderr
print(
    "Benchmark CSV: exact frame records, finite costs, bounded uploads and file errors PASS"
)
