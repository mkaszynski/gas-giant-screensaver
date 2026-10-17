# Inland specialization: retained runtime evidence

**Performance acceptance failed.** This archive records the complete 24-run
matched trial, four longer memory probes, and native submission measurements.
It does not certify Landscape Phase 2's 30 fps or +30% renderer cost target.
The renderer and tool commits, exact commands, binary hashes, controls and
artifact SHA-256 hashes are in [result.json](result.json).

## Matched cost trial

Three alternating baseline/candidate pairs cover ocean, forest, snow and coast
at 1920×1080. The baseline is frozen Phase 1 commit
`f882d4d701308b06c25be0a2118871dc244f9794`. The candidate uses thin clouds to
match the original landscape scope; default volumetric weather is measured
separately. Neither renderer's visual quality settings were reduced.

Every collected run is retained. Complete-renderer summary costs include all
180 frames in both versions. Deadline counts exclude the first ten candidate
frames, as declared before collection. The immutable baseline does not export
per-frame CSV, so its original summary log is retained instead.

| Biome | Median candidate/baseline | p95 candidate/baseline |
| --- | ---: | ---: |
| Ocean | 0.920 | 1.118 |
| Forest | 1.090 | 1.041 |
| Snow | 0.971 | 1.036 |
| Coast | 1.327 | 0.975 |

These aggregate ratios are descriptive; acceptance requires **each pair** to
remain within 1.30 and every steady candidate frame to meet 33.333 ms.
The trial has 214 steady deadline misses and fails both gates. Host workload
controls passed, but the collector itself repeatedly enumerated GPU file
descriptors, taking up to 230 ms per sample.

The [exploratory correlation](diagnostics/sampler-correlation.json) finds that
213 of 214 misses overlap these reads. This is an association, not proof of
causality. No frames have been removed or relabeled as passing. A separate
off/full/off/cached collector experiment and a fresh matched trial are needed.
Cached collection must also meet its declared 10 ms active read limit.

`frames/` contains all candidate frame CSVs, both versions' stdout logs and
reduced telemetry. Telemetry retains sampling times, read durations, elapsed
time and CPU/GPU counter deltas sufficient to recompute workload averages;
unrelated process details are omitted. Gzip files use a fixed zero timestamp.

## Longer probes and native path

`memory/` contains the four 720-frame forest/coast CSVs and logs, with and
without volumetric weather, plus every sampled RSS/high-water value. Peak RSS
was 361.34 MiB. RSS does not include every GPU allocation. Each probe crossed
four mesh revisions; normal vertex uploads stayed at or below 2 MiB per frame.
The coast still had two thin-cloud deadline misses and one weather miss;
these are retained and are not explained away by the collector hypothesis.

The exact packaged native renderer submitted 300 surfaces in ten measured
seconds (30.0002 Hz) after warmup, on a private 1920×1080 output with an awake
physical desktop parent. All submission intervals are in `result.json`.
Submission cadence does not establish physical presentation cadence.

## Rejected presentation timestamps

Both physical fullscreen runs returned nonmonotonic presentation timestamps.
Their complete feedback CSVs and rejection logs are in `diagnostics/`; neither
run is accepted as presentation evidence. Timestamps were not sorted, clipped
or otherwise repaired to manufacture a passing result.

An offline reproduction using upstream Hyprland v0.52.1's
[Time.cpp](https://github.com/hyprwm/Hyprland/blob/v0.52.1/src/helpers/time/Time.cpp)
and [Time.hpp](https://github.com/hyprwm/Hyprland/blob/v0.52.1/src/helpers/time/Time.hpp)
converted one fixed steady-clock input 100,000 times. Results varied by about
one second and became nonmonotonic 32,720 times. Source hashes and URLs are in
the report; only our small reproduction driver and its result are included.
To reproduce, fetch those exact files, verify their hashes, then compile them
with `diagnostics/hyprland_time_reproducer.cpp` using C++23. Counts depend on
timing; a fixed input should always produce one identical output timestamp.
The running compositor and NixOS configuration have not been modified.

The PR remains a candidate awaiting performance validation and operator
review. Hardware/software image regression checks and passing CI establish
rendering correctness, not completion of the outstanding performance gates.
