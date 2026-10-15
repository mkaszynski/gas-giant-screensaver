# Background terrain build cost

The airborne landscape keeps its existing 512,800 vertices, 3,072,000 terrain
indices, numerical materials and shaders. This change removes repeated CPU work
from construction of each replacement mesh. It does not reduce resolution,
change landforms, throttle the worker, or change system power settings.

Measured code: candidate `4f880f92e88396a209313cc604142b5bcef19a5c`, compared with
`d39c0e9` using the same benchmark instrumentation. Both builds use the Nix
development environment and CMake Release flags. Renderer comparisons use the
same immutable approved Phase 2 asset directory recorded in their reports.

## Changes

* A geographic sample computes its grid coordinates, height, moisture, uplift,
  seam factor and landform weights once. Elevation and material sampling share
  those values instead of repeating the interpolation and weighting work.
* Each mesh computes its 800 azimuth sine/cosine pairs once, and its spherical
  scale/curvature once per radial ring. The arithmetic and resulting values are
  retained.
* Small coarse-water fans use a bounded 16-entry local array. Their indices are
  reserved in advance, avoiding repeated small heap allocations. Their topology
  and boundary vertices are unchanged.

## Exact geometry and CPU measurements

Four alternating before/after pairs per fixture measure only mesh construction,
excluding asset loading, export and hashing. All 32 builds export identical
serialized geometry for their respective fixture. The export includes vertices
with normals/material coordinates, terrain/water indices, bounds and index
ranges, route center and land/water flags. It avoids uninitialized struct
padding. These are local compiler/platform comparisons, not portable mesh hashes
across different math-library implementations.

| Fixture and flight seconds | Before median ms | After median ms | Reduction |
| --- | ---: | ---: | ---: |
| Ocean, 0 | 415.563 | 324.314 | 22.0% |
| Coast, 5600.390625 | 413.761 | 325.085 | 21.4% |
| Forest, 162000 | 352.059 | 259.746 | 26.2% |
| Snow, 210600 | 361.541 | 269.198 | 25.5% |

[All CPU timings and geometry hashes](terrain-build-cost/mesh-paired.json) retain
the individual runs. [The export probe](terrain-build-cost/mesh-export.cpp) links
against each revision's `build/libobservatory.a`; pass its asset directory and
flight seconds. Hash its binary stdout with SHA-256; construction milliseconds
are written separately to stderr. Compile it in the Nix development environment
with C++20, the project's `src` include directory and the GLFW/GLES/libpng link
flags used by the main executable.

## What the diagnostic controls establish

A stationary renderer plus a separate process replaying the actual mesh builder
isolates CPU construction from mesh upload or replacement. Four builds take
324–361 ms each and increase nearby frame costs from approximately 13 ms to
18–21 ms. This confirms background CPU work can slow rendering on this shared
APU. It does not identify whether power, memory bandwidth, or another shared
resource accounts for every extra millisecond.

The initial moving diagnostic still records three steady deadline misses and a
63.494 ms maximum. Its worst frame spends 0.965 ms submitting commands and
62.529 ms in the final wait. Clock samples are low during that interval. These
observations narrow the investigation; they do not establish a causal governor
fault. The fixed-observer traces from the earlier investigation also remain
valid and rule out terrain streaming as the sole possible stall cause.

The [worker replay source](terrain-build-cost/worker-replay.cpp) performs real
builds without creating a GL context or uploading them. Its event times, the
initial slow trace and the stationary controls remain in the archived records.

## Sampled renderer comparisons

Three alternating pairs run 720 frames each at 1920×1080, 30 fps, sky time 510,
flight time 162000 and speed 1. The first ten frames are excluded from steady
statistics. Each run starts five mesh builds, including its initial synchronous
build and four replacements. Both physical outputs report DPMS off at the
initial snapshot; these are synchronized hidden-window render costs, not
presentation timings. Output state was not continuously sampled.

| Pair | Before median / p95 / max ms | After median / p95 / max ms | Before / after deadline misses |
| --- | --- | --- | ---: |
| 1 | 13.300 / 17.118 / 21.471 | 13.267 / 15.974 / 21.897 | 0 / 0 |
| 2 | 13.311 / 18.182 / 21.513 | 13.296 / 15.805 / 29.325 | 0 / 0 |
| 3 | 13.293 / 17.851 / 20.878 | 13.324 / 16.323 / 21.409 | 0 / 0 |

Worker-pending observations fall from 41/42/42 frames to 32/32/32. The stationary
control remains effectively unchanged: 13.007/14.327 ms median/p95 before,
13.027/14.308 ms after. The improvement is a shorter period of interference;
individual active-worker frames do not become uniformly cheaper. Other desktop
work and automatic GPU clocks are not controlled.

High-rate telemetry has a measurable footprint: roughly three sampler CPU
seconds over each 25-second run, with sensor reads around 1.1 ms per 10 ms
sample. It is a diagnostic tool, not part of ordinary rendering. A direct /
sampled / sampled / direct stationary control has comparable medians and no
steady deadline misses, but cannot prove every timing tail is unaffected.
Unmonitored moving comparisons are therefore retained separately.

## Unmonitored renderer comparisons

A second three-pair series uses the same 720-frame moving fixtures and alternates
order in the same way, with no telemetry process or sensor polling. These
comparisons confirm the p95 improvement independently of the probe. As in the
sampled series, neither reference nor candidate misses a steady deadline in
this series; this cannot demonstrate elimination of all earlier stalls.

| Pair | Before median / p95 / max ms | After median / p95 / max ms | Before / after deadline misses |
| --- | --- | --- | ---: |
| 1 | 13.313 / 18.003 / 21.953 | 13.303 / 15.508 / 21.676 | 0 / 0 |
| 2 | 13.361 / 18.501 / 23.370 | 13.260 / 15.972 / 21.589 | 0 / 0 |
| 3 | 13.311 / 18.269 / 23.596 | 13.240 / 15.414 / 23.496 | 0 / 0 |

## Interpreting the benchmark fields

`--benchmark-output` retains its existing fields and appends:

| Field | Meaning |
| --- | --- |
| `submit_ms` | Wall time issuing renderer commands, including CPU work and any blocking GL calls. It is not CPU utilization. |
| `wait_ms` | Wall time ending the GPU query, finishing outstanding work and reading its result. |
| `monotonic_seconds` | Steady-clock render start, aligned with Linux monotonic telemetry timestamps. |
| `mesh_builds_started` | Cumulative synchronous and asynchronous mesh constructions. |
| `mesh_build_pending` | Whether the background job is unfinished at log time, after the GPU wait. A job that finishes during the frame can report zero. |

Submission and wait durations sum to `render_ms`. GPU elapsed queries can include
gaps between CPU command submissions and other GPU work; they are not a direct
busy-time measurement. The profiler retains preceding/following sensor samples
when none fall inside a frame, and records the sensor batch's read duration.
Never assume the GPU held a sampled frequency for an entire frame. Process CPU
ticks have 10 ms granularity on this machine. The device power sensor is labeled
PPT; it has not been calibrated as isolated GPU power. The initial trace files'
legacy `gpu_power_microwatts` key does not establish GPU-only energy use.

```sh
python3 scripts/profile_frame_stalls.py --frames 720 --travel-speed 1 --output-prefix /tmp/terrain-profile
```

The script writes raw frame CSV, raw telemetry JSONL, a summary and a renderer
log. Choose `--gpu-device` on a system with multiple DRM devices; unreadable
sensors remain missing rather than being reported as zero. `--no-telemetry`
omits sensor reads; a direct executable run also avoids the sampler's process
polling. No power-management files are written.

The [record manifest](terrain-build-cost/files.json) lists SHA-256 hashes of the
uncompressed archived records. Frame CSV and telemetry JSONL files are gzip
compressed without changing their contents. Summary files retain individual
timing tails, binary hashes and filtered output state where available.

## Package, native and memory checks

The full local gate passes: 40 CPU/CLI checks, 38 offline model/asset checks,
seven profiler tests, all 13 GPU test programs on AMD and Mesa software, and
benchmark CSV integration on both backends. The published CPU probes compile;
their argument checks and geometry export are verified. Nix flake checks and
standalone/native builds pass.

The packaged candidate and approved renderer produce identical RGB pixels at
1920×1080, sky time 900 and flight time 162000, each using its own installed
assets. [Decoded image hashes](terrain-build-cost/image-comparison.json) and
the [candidate preview](terrain-build-cost/preview.png) retain the comparison.
The native package renders a valid full-size scene on an owned headless output
and exits through its signal-driven unlock path. PAM authentication is not
exercised. Its twenty-second trace measures 29.957 submissions/s, 34.708 ms p95
and 53.571 ms maximum interval. This branch retains main's native scheduling;
the separate, unmerged timer fix is [PR 16](https://github.com/mkaszynski/gas-giant-screensaver/pull/16).
Native submissions are not physical presentation measurements.

The packaged 720-frame memory run reaches 354.9 MiB peak process RSS, starts five
builds and completes five mesh revisions. Normal uploads remain at most 2 MiB
per frame. RSS excludes some GPU residency, and this finite run is not a proof
of indefinite memory stability; mesh/cache dimensions and pending job counts
remain structurally bounded. The complete samples and frames are retained.

Importantly, that run **does not pass the deadline target**: median 17.973 ms,
p95 30.711 ms, maximum 46.651 ms, and 23/710 steady frames over 33.33 ms. It is
not replaced by the faster source-build comparisons.

A subsequent control confirms the installed Phase 1 lock is concurrently using
the GPU after the display wakes. A single client counter from
`/proc/<lock-pid>/fdinfo/<fd>` accumulates 2.741 seconds of graphics-engine work
during a 7.365-second rendering run. Duplicate file descriptors are not summed.
One physical output reports DPMS on at both endpoints. The foreground candidate
measures 17.720 ms median / 28.142 ms p95 / 36.307 ms maximum with 3/170 misses.
The [counter/control report](terrain-build-cost/competing-renderer.json) and its
frame records retain this shared-renderer condition. Linux documents these
per-client counters in [DRM usage stats](https://docs.kernel.org/gpu/drm-usage-stats.html).

This establishes concurrent GPU work during that control, not a retrospective
attribution of every earlier miss. The changing output/lock state reinforces
the need for a clean unlocked-desktop run before final acceptance.

## Acceptance still open

These measurements establish a CPU efficiency improvement with unchanged
geometry. They do not establish uninterrupted 30 fps, clean-desktop presentation
or complete Phase 2 performance acceptance. Preserve the original Phase 1
baseline and the original slow Phase 2 measurements. The fresh physical-output,
multi-output and complete-renderer budget checks require an unlocked desktop;
the installed lock and sleeping outputs are not replaced by this benchmark.

The remaining stall investigation is tracked in
[issue 13](https://github.com/mkaszynski/gas-giant-screensaver/issues/13).
Operator review precedes merging or changing the NixOS pin.
