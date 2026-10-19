# Native cloud compute

The native cloud pass can execute the existing weather shader as an OpenGL
compute program. Its 16×4 workgroups reduce the measured cost of the expensive
volume integration on the development computer. This is an execution change:
the 96-interval marcher, Gaussian density samples, lighting, atmospheric
attenuation, independent weather clock, fog, rain, cirrus, and MSAA edge columns
remain shared with the fragment path. There is no temporal reconstruction,
resolution reduction, or new approximation to cloud density.

## Complete columns over opaque terrain (performance validation pending)

The native cache can reuse a dense cloud interior over fully opaque terrain
when the entire cloud column ends before the terrain. A column clipped by a
mountain, a partially covered silhouette, a clear column or a thin wisp keeps
its original display-resolution trace. No new pass, texture or resolution
change is introduced. The 16×4 workgroups remain unchanged.

The coarse native pass integrates complete columns even over terrain. The sign
of its half-float transmission records terrain coverage, including negative
zero; the magnitude retains the original transmission precision. Sky rays still
reject every footprint touching terrain. Opaque-ground reuse is enabled only
when both native compute passes are available, and decodes that sign before
interpolation. Fragment/browser paths and unavailable-compute fallbacks retain
the previous cache behavior.

Ground reuse requires all four cached transmissions to be at most 0.2. This
protects clear gaps and thin skins between cache rays. Validation uses the
actual shaded terrain background and additionally restricts projected neighbor
contrast to `0.5/255` relative to the minimum (with a `1e-6` dark floor).
For the existing ACES rational response followed by gamma, `x*f'(x) < 2/2.2`;
positive foreground fog cannot increase relative contrast. Ground paths that
could intersect cirrus remain exact: height is convex along a ray, so checking
both endpoint heights against 9.5 km conservatively excludes that layer.
These bounds supplement rendered comparisons; they do not prove that every
unsampled feature lies between the four sampled values.

All 45 hardware compute/fragment comparisons pass with at most one display-code
difference (maximum case mean 0.00205 codes). A homogeneous opaque-slab test
observes 34,905 complete ground columns actually reused, 499 partial pixels
traced directly, and 1,004 rejected sky footprints touching terrain. A second
slab clips the columns at 1.5 km and verifies that interrupted ground rays never
reuse the cache. The sign mask is exercised with transmissions that round to
half-float negative zero. The full weather behavior program passes its existing
cache, opacity, altitude, shadow, lightning, continuity and MSAA checks.

Software, package/native, memory and controlled timing validation are pending.
The timing trial stopped after its first baseline run exceeded unchanged host
controls (1.66 background CPU cores and 16.2 ms monitoring latency); no candidate
timing or sustained-30-fps claim is made. Preserve the frozen Phase 1 baseline
and the outstanding acceptance gates in `LANDSCAPE_PHASE2.md`.

Two earlier experiments are not retained in the active implementation. A 32×2
layout passed image comparisons but did not establish a speed benefit. Splitting
integration/composition reduced register pressure and preserved images, but
added 63.28 MiB and showed no measured benefit before its paired trial failed
background-load controls. The original single-pass solver and buffers remain.

The coarse sky cache can also execute the same solver as a specialized compute
program, keeping its original resolution and display-pixel ray footprint.
Native compute
requires suitable image/sampler limits and full-float render targets. Unsupported
contexts retain the fragment path. Browser WebGL 2 and WebGPU continue using
their existing programs.

## Calm weather specialization

Native compute also caches a calm variant of the same solver. It compiles out
storm anvils and lightning only when both CPU inputs are exactly zero. Any
nonzero storm strength or flash immediately selects the full program, including
a backwards debug time seek. Coverage, stratus, cirrus, fog, precipitation,
quadrature and all lighting remain dynamic. Both compute passes cache calm
and full variants; surface shaders and browser programs retain the original
model. Failure to compile the optional
variant keeps the full compute program. The calm specialization itself adds no textures or passes.

Four alternating 90-frame forest diagnostics measured cloud medians of
15.422 → 13.317 ms (13.7% lower), and complete-renderer medians of
33.626 → 31.746 ms (5.6% lower). These are medians of run medians; p95 frames
still exceeded 33.33 ms. Every run failed at least one host-load or collector
control. [All raw frames and control results](native-calm-compute/result.json)
are retained; these measurements do not establish sustained 30 fps.

## Preserving raster sampling

Recomputing a fragment's UV by division in a compute shader can differ from the
rasterizer's interpolation by one float ULP. At an exact sky-cache cell boundary,
that can select a different set of neighbors and change the cache's reuse
decision. An initial implementation exposed three pixels beyond the two-code
image limit at 1920×1080.

An immutable RGBA32F coordinate cache captures the rasterizer's UV and
sky-cache cell coordinates with the original fullscreen vertex shader,
viewport and framebuffer dimensions. Mesa exposed six channels beyond the
image limit with separable coordinate strips: interpolation varies by a few
float bits across rows. Capturing just one row and column is insufficient,
even with a full-size temporary framebuffer. The complete cache preserves
those decisions without changing the reference fragment shader.

Storage is 31.64 MiB for display coordinates and 5.27 MiB for sky-cache
coordinates at 1920×1080, allocated only for native compute and
regenerated only on first use or resize. One exact texel fetch replaces the
previous two strip fetches. No sampling resolution, cloud quality, history or
per-frame uploads change. The added memory is bounded by the current viewport;
resizing releases the old cache. [Four-biome diagnostics](native-cloud-compute/coordinate-cache.json)
retain both coordinate strategies and the original fragment solver; host load
varied, so those results do not isolate a small allocation/filtering cost.

Weather outputs retain RGBA16F storage. Image writes require immutable texture
storage on OpenGL ES. Image-unit bindings are restored after dispatch, and the
memory barrier covers subsequent texture sampling and framebuffer blending.
The secondary weather attachment is written at partially covered terrain pixels,
the only pixels where the compositor consumes it. Newly exposed edge pixels
are updated before composition or manual meteor blending.

## Verification

`weather-compute-tests` compares the complete compute and fragment images with
the existing maximum difference of two display codes and mean difference of
0.02. It exercises 1080p forest, coast, snow and ocean; all weather presets;
day/night lighting and lightning; manual meteors; odd sizes; independent width
and height changes; moving MSAA silhouettes; disabled sky caching; and
preservation of nondefault host image bindings. Calm/full selection and
backwards seeks through a lightning pulse are checked on the same renderer.

Both compute passes passed all 41 comparisons on AMD and all 41 on Mesa
with zero differing display codes, including cache-enabled and uncached
rendering, storm transitions and lightning. Software-rendered benchmark CSV,
profiler, live debug controls and ES 3.0 fragment fallback also passed.
All 58 CPU/CLI checks, the 18 existing native rendering suites, tooling tests
and native benchmark checks passed. Final Nix builds and CI remain pending.

[Eight alternating 90-frame sky-compute diagnostics](native-cloud-compute/sky-compute.json)
measured these medians of run medians at 1920×1080:

| Scene | Previous complete frame | Sky-compute complete frame | Previous sky pass | Compute sky pass |
|---|---:|---:|---:|---:|
| Coast | 35.654 ms | 33.543 ms | 6.673 ms | 4.148 ms |
| Forest | 31.228 ms | 29.851 ms | 2.238 ms | 0.818 ms |

These include seven profiling queries; the ordinary app is measured separately.
Some host-load controls failed and p95 frames exceeded 33.33 ms. These results
show the optimization's direction but do not establish sustained 30 fps or the
original Phase 1 cost allowance. All frames and failed controls are retained.

For a direct comparison, use identical dimensions, scene/route/weather clocks,
and `--benchmark` settings, adding `--no-weather-compute` for fragment execution.
The benchmark reports the selected cloud backend. Keep the frozen Phase 1
comparison separate from this cloud-only optimization comparison.
