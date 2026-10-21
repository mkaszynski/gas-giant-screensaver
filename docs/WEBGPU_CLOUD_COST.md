# WebGPU cloud shader and binding cost

The final weather pass is the largest measured browser GPU cost. This change
reduces shader expansion and repeated host-side binding work while preserving
resolution, cloud layers, noise, quadrature nodes, weights and march limits.

## Shared density lookup loop

The fragment marcher retains the original midpoint/two-point Gaussian blend. Intermediate
steps still evaluate three taps, long steps two and very short steps one. A
small dynamic loop shares the density lookup body instead of expanding it for
every tap in the generated WGSL. Density and its first moment use the original
arithmetic order; lighting still uses the same density-weighted centroid.

`weather_quadrature.glsl` contains the production implementation. The GPU tests
replace it with the frozen, separately expanded reference in
`tests/weather_quadrature_reference.hpp`. Compute/fragment and fragment/fallback
comparisons therefore cannot silently compare the new loop against itself.
The existing image tolerances are unchanged. Native compute retains its original
expanded lookup block; this optimization targets fragment shader generation.
All six native compute variants have identical preprocessed tokens to the
merged baseline; [their fingerprints](webgpu-cloud-cost/native-compute-tokens.json)
record this check, separately from runtime performance.

## Calm fragment variants

Native compute already specializes weather with exactly zero storm and flash
radiance. The fragment path now has equivalent optional final/sky variants.
C++ selects them only when `WeatherState::calm()` is true; there is no epsilon,
weather quantization or browser-side model decision. Nonzero storm or flash
inputs immediately select the complete model, including after time seeks.
Cloud cover, stratus, rain, ice clouds, fog and all lighting remain dynamic.

Each selected program receives its own complete uniform setup. A failed
optional compilation uses the existing full final/sky programs. The offline
WebGPU catalog contains both extra variants, bringing its total to 32.
CPU tests cover tiny nonzero inputs; GPU tests exercise changing weather on
the same renderer and deliberate optional-program failures.

## Texture group reuse

The WebGPU command adapter keeps one current texture-state record per program.
Unchanged draws reuse their existing bind group without reconstructing texture
views, sampler descriptors and JSON keys. Every draw still binds the group to
its current render pass and uploads its current dynamic uniforms.

Texture identity, storage generation, sampler changes and sampler-unit changes
invalidate the fast path. Repeating an identical texture parameter preserves
it. The existing 64-entry per-program bind-group cache and GPU resource limits
remain unchanged. No new render targets, texture formats or scene passes are
introduced. Two additional shader variants can affect compilation/driver memory;
the full before/after memory matrix remains required.

## Idle-desktop hardware comparison

After the operator unlocked the desktop and confirmed it was idle, the actual
baseline and production builds ran in ABBA order on the AMD hardware adapter.
Each run used High quality, a 1920×1080 scene and display, all default effects,
60 completed warmup frames and 300 measured completed frames. The scene was
`time=510&travel=162000&weather=1790900000&speed=1`. The unchanged process-tree
envelope was 1100 MiB and 90% of one CPU, with a 768 MiB system reserve.
There was no installed lock-screen process in the preflight and no clock or
power changes. The baseline artifact predates the merged baseline's final
documentation commits; its runtime sources are unchanged.

| Run | Completed frames/s | Median CPU encoding | p95 CPU encoding |
| --- | ---: | ---: | ---: |
| Baseline A1 | 20.09 | 10.73 ms | 12.55 ms |
| Candidate B1 | 20.50 | 8.40 ms | 10.20 ms |
| Candidate B2 | 20.18 | 8.25 ms | 10.07 ms |
| Baseline A2 | 20.53 | 11.35 ms | 14.44 ms |

CPU encoding medians improve about 25%, but completion throughput is effectively
unchanged around 20.3 fps. This does **not** establish a GPU speedup or satisfy
the browser's 30 fps requirement. Completion timing is not physical screen
presentation timing; CPU quota throttling is recorded in every run.

Separate 24-frame GPU timestamp runs measured the final weather pass at
22.06 ms before and 23.17 ms after, including its clear-row draw. Total pass
time was 44.83/46.29 ms. These instrumented runs are distinct from the ABBA
throughput samples and provide no evidence of reduced GPU cost. The terrain
cloud solver remains the largest GPU cost. A further conservative empty-cell
lookup probe did not improve it and is excluded from the implementation.

The [numeric record and compressed completed-frame traces](webgpu-cloud-cost/idle-abba.json)
retain artifact hashes, adapter, dimensions, quality, throttling and read-only
GPU telemetry. Original contended probes are excluded: a separate two-second
DRM counter sample measured 1.25 seconds of installed lock-screen GPU work.
See [the preceding baseline](BROWSER_GPU_TIMING.md) for earlier measurements.

## Correctness validation

The native application builds and all 72 CPU/CLI checks pass. All 13 portable
test commands pass, including variant conversion and adapter invalidation
cases. The production WASM build succeeds. Direct GPU readback of frozen
1080p daylight, storm and lightning scenes is byte-identical to the baseline:
all 8,294,400 RGBA bytes match in each scene. These captures use the actual
production catalog and runtime, without replacement shader manifests.
Full renderer/backend/native-parity/Nix/memory CI remains the release gate.
