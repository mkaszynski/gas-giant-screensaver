# Coastal weather performance follow-up

This branch follows the weather integration beta. Installation preparation is
conditional on repeated 1920×1080 rendering measurements meeting the 30 fps
budget. Main and the installed NixOS pin remain unchanged during development.

The far sunlight column uses one weighted-centroid probe instead of two. It
retains the original total weight (2.3) and first moment (2.55 step lengths), so
constant and linear columns have the same estimate. Nearby cached sunlight and
local extinction are unchanged. A rendered-reference regression checks nonlinear
coastal lighting at three flight times against the original two-probe formula.

The sky optical cache now checks the combined cloud emission and transmitted
background range. A resolved bright planet no longer automatically forces a
full ray march through otherwise smooth clouds. Very bright sources and a
5.7-degree region around the Sun retain direct extinction, including its halo.
The relative radiance criterion is a reconstruction heuristic, not a universal
error proof; moving reference comparisons continue to enforce image fidelity.
Ground and partially covered terrain retain direct integration. No ray-march
intervals or terrain detail are removed; there is no temporal cloud history.

## Development profiling

Normal screensaver and Hyprlock builds contain no profiling queries. Enable the
optional development executable with:

```sh
nix develop --command cmake -S . -B build -DOBSERVATORY_BUILD_WEATHER_BENCHMARK=ON
nix develop --command cmake --build build --parallel 2
./build/weather-benchmark . 1920 1080 360 1740 5600.390625 1790900000 0.0333333333333333 1 > /tmp/coast-stages.csv
```

Arguments are data directory, width, height, measured frames, starting sky time,
flight position, weather clock, scene step, and cache enable (0/1). Thirty warmup
frames hold the initial scene fixed. Measured frames advance by the explicit
step independently of rendering speed. A zero step freezes the scene. The
executable emits individual GPU times for atmosphere, celestial objects, terrain,
sunlight cache, sky optical cache, full-resolution weather and composition, plus
total synchronized CPU/GPU time. Results exclude physical presentation. Additional
queries can affect scheduling; uninstrumented packaged runs remain the acceptance
measurements.

The local matched 360-frame development probe improved from 25.73 ms median to
24.35 ms. This is diagnostic evidence, not the installation acceptance result.
Seven initial 640×360 captures differed from the old renderer by at most 14/255
in a channel, with average channel differences below .04/255. Full local gates,
packaged repeated comparisons and installation preparation are still in progress.
