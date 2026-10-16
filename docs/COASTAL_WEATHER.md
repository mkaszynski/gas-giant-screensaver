# Coastal weather performance follow-up

This branch follows the weather integration beta. Installation preparation is
conditional on repeated 1920×1080 rendering measurements meeting the 30 fps
budget. Main and the installed NixOS pin remain unchanged during development.

The far sunlight column uses one weighted-centroid probe instead of two. It
retains the original total weight (2.3) and first moment (2.55 step lengths), so
constant and linear columns have the same estimate. Nearby cached sunlight and
local extinction are unchanged. A rendered-reference regression checks nonlinear
coastal lighting at three flight times against the original two-probe formula.

The sky optical cache checks each corner's combined cloud emission and
transmitted background. Emission and transmission are projected together, so
opposing changes that cancel in the final color do not invent a contrast edge.
A regression uses alternating optical columns to verify reuse over a matching
background and rejection when the background makes those columns visibly differ.
Very bright unresolved sources and a 5.7-degree region around the Sun retain
direct extinction, including its halo.

The cache uses one third of display width and half its height (640×540 at
1080p, about 2.64 MiB). Keeping vertical sampling is deliberate: a uniformly
smaller 640×360 cache failed the existing moving-reference error limit. The
final weather output, noise footprints, terrain coverage, fog and thin high
clouds remain at display resolution.

The reconstruction contrast threshold remains 2% for grazing rays, rising smoothly
to 24% between elevations of approximately 1.1 and 3.4 degrees. This is the range
across the coarse corners in exposed linear radiance, not a permitted display-pixel
error. The stricter horizon band protects long paths and fine silhouettes. Moving
cached/direct comparisons retain their original peak 16/255 and mean .15/255
channel limits. The criterion is a reconstruction heuristic, not a universal error
proof. Ground and partially covered terrain retain direct integration. No
ray-march intervals or terrain detail are removed; there is no temporal cloud
history.

## Installation acceptance

At 1920×1080 with a 30 fps cap, the per-view 95th-percentile complete CPU/GPU
rendering time must be at most 33.33 ms across three alternating pairs of all six
reference views. Ten startup frames per run are excluded from that steady figure;
raw startup costs, every missed deadline and maxima remain reported. A separate
900-frame coastal flight must also meet the p95 budget, followed by native
Hyprlock verification. This criterion does not assert zero long frames or measure
physical presentation. The NixOS installation update stays in draft until these
measurements pass. No system switch is performed during validation.

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

The initial matched 360-frame development probe (`74c686a`) improved from
25.73 ms median to 24.35 ms. That instrumented result was not sufficient for
installation acceptance: its packaged coastal p95 was 37.16 ms. Correlated cache
reuse subsequently brought the coast to 29.25 ms median, but its 34.96 ms p95
also failed the criterion. Both failed datasets remain available with the final
local validation artifacts; results are not discarded or selected by best run.

Current measured results, source revisions, full-gate status and review commands
are recorded in [screensaver PR 19](https://github.com/mkaszynski/gas-giant-screensaver/pull/19).
The exact public source pin and lock-file update are in
[NixOS PR 18](https://github.com/mkaszynski/nixos-config/pull/18). Main and the
installed system remain unchanged pending operator review.
