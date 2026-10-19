# Coast rendering headroom

This change skips results which the existing water shader discards. An interior
reflected scene sample with confidence exactly one replaces the atmospheric sky
completely; its fallback sky lookup is deferred until needed. A reflected cloud
column with opacity exactly zero returns its incident radiance before computing
unused cloud lighting. The density field, edge fades, solar-disk treatment,
reflections, geometry, wave filtering and lighting equations are retained.

`water-reuse-tests` compares the previous evaluation order against the optimized
path. All 54 image comparisons are pixel-identical on the target AMD APU. Cases
cover ocean, coast and forest views, day/twilight/night, clear/fair/storm weather,
legacy clouds, optional terrain reflections and a 1080p coastal view.

## Measurements

The development profiler now separates terrain preparation, shadow-map updates
and terrain shading, and records mesh revisions, shadow revisions, uploads and
worker activity. Normal rendering contains no profiling queries.

The paired ABBA diagnostic uses 720 frames per run at native 1920x1080, all
effects and default emission brightness, with fixed 1/30-second simulation steps.
Thirty identical-scene prewarm frames precede all 720 recorded rows; none of
those recorded frames are removed. A static fullscreen cover, stable outputs
and GPU clients, less than one background CPU core, less than 5% external GPU
work and at most 10 ms active monitoring latency bound desktop interference.
All four runs pass those controls. Firmware sensors are not polled while rendering.

| Run | Median ms | p95 ms | Frames above 33.33 ms /720 |
| --- | ---: | ---: | ---: |
| Before 1 | 29.03 | 31.19 | 16 |
| After 1 | 28.24 | 31.08 | 13 |
| After 2 | 27.96 | 30.62 | 12 |
| Before 2 | 29.57 | 31.45 | 11 |

The improvement is additional rendering headroom, **not elimination of every
stall**. Several slow frames affect multiple GPU stages without an upload or
shadow update; some also occur without a pending terrain build. These traces do
not establish a driver or power-governor cause. No clocks, power settings,
monitoring limits or quality settings were changed.

[Paired metadata and hashes](coast-headroom/paired.json) and the compressed raw
CSV files in that directory retain all samples. These GPU-stage diagnostics do
not measure physical scanout, and are not a strict no-stall performance pass.

Reproduce an unmonitored diagnostic with the optional profiling target:

```sh
nix develop --command cmake -S . -B build -DOBSERVATORY_BUILD_WEATHER_BENCHMARK=ON
nix develop --command cmake --build build --target weather-benchmark --parallel 2
./build/weather-benchmark . 1920 1080 720 1740 5600.390625 1790900000 0.03333333333333333 1
```

For an independent complete-renderer comparison, use the packaged executable's
`--benchmark`/`--benchmark-output` options with matching clocks, effects and route.
