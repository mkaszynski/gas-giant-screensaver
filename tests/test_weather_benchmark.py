"""Validate the optional GPU profiler's deterministic scene clocks and stages."""

import csv
import io
import math
import subprocess

stages = {
    "atmosphere_ms", "celestial_ms", "terrain_ms", "solar_cache_ms",
    "sky_cache_ms", "clouds_ms", "composite_ms",
}
for step, cache in [(0, 1), (1 / 30, 0)]:
    run = subprocess.run(
        ["./build/weather-benchmark", ".", "128", "72", "3", "1740",
         "5600.390625", "1790900000", str(step), str(cache)],
        text=True, capture_output=True, check=True,
    )
    rows = list(csv.DictReader(io.StringIO(run.stdout)))
    assert len(rows) == 3, run.stdout
    assert set(rows[0]) == stages | {"frame", "scene_seconds", "render_ms"}
    for i, row in enumerate(rows):
        assert int(row["frame"]) == i
        assert abs(float(row["scene_seconds"]) - (1740 + i * step)) < 1e-7
        for stage in stages | {"render_ms"}:
            value = float(row[stage])
            assert math.isfinite(value) and value >= 0, row
        assert float(row["clouds_ms"]) > 0, row
        if cache:
            assert float(row["sky_cache_ms"]) > 0, row
print("Weather profiler: deterministic clocks, finite GPU stages and cache selection PASS")
