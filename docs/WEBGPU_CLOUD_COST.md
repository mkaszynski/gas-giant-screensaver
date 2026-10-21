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

## Validation status

The native application builds and all 72 CPU/CLI checks pass. Portable checks
include the new variant conversion and adapter invalidation cases. A frozen
1080p daylight shader probe preserved all 8,294,400 display bytes; the production
build still needs its full renderer/browser/memory CI and final hardware checks.

The initial hardware probes ran alongside the installed lock screen. A separate
two-second DRM client-counter sample measured 1.25 seconds of lock-screen
GPU work. Those timings are diagnostics, not controlled performance acceptance;
no FPS improvement or 30 fps claim is based on them. Final before/after timings
require an unlocked, idle desktop and the unchanged local resource envelope.
See [the preceding baseline](BROWSER_GPU_TIMING.md) for earlier measurements.
