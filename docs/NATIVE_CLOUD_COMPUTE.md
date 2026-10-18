# Native cloud compute

The native cloud pass can execute the existing weather shader as an OpenGL
compute program. Its 16×4 workgroups reduce the measured cost of the expensive
volume integration on the development computer. This is an execution change:
the 96-interval marcher, Gaussian density samples, lighting, atmospheric
attenuation, independent weather clock, fog, rain, cirrus, and MSAA edge columns
remain shared with the fragment path. There is no temporal reconstruction,
resolution reduction, or new approximation to cloud density.

The coarse sky cache still uses its original fragment program. Native compute
requires suitable image/sampler limits and full-float render targets. Unsupported
contexts retain the fragment path. Browser WebGL 2 and WebGPU continue using
their existing programs.

## Calm weather specialization

Native compute also caches a calm variant of the same solver. It compiles out
storm anvils and lightning only when both CPU inputs are exactly zero. Any
nonzero storm strength or flash immediately selects the full program, including
a backwards debug time seek. Coverage, stratus, cirrus, fog, precipitation,
quadrature and all lighting remain dynamic. The sky cache, surface shaders and
browser programs retain the original model. Failure to compile the optional
variant keeps the full compute program. No textures or passes are added.

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

Storage is 31.64 MiB at 1920×1080, allocated only for native compute and
regenerated only on first use or resize. One exact texel fetch replaces the
previous two strip fetches. No sampling resolution, cloud quality, history or
per-frame uploads change. The added memory is bounded by the current viewport;
resizing releases the old cache. Its frame-time impact still requires a fresh
measurement; the calm timings above predate this portability correction.

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

The full coordinate-cache prototype passed all 41 comparisons on Mesa with
zero differing display codes. The calm variant also passed AMD comparisons
with differences of zero or one code before this portability correction.
Final hardware, full regression, build, browser, Nix, CI and controlled performance gates
are still required before release. Exploratory prototype timings on a shared
desktop reduced cloud time from about 20 ms to 16 ms; those measurements do not
establish a sustained 30 fps result for the complete renderer.

For a direct comparison, use identical dimensions, scene/route/weather clocks,
and `--benchmark` settings, adding `--no-weather-compute` for fragment execution.
The benchmark reports the selected cloud backend. Keep the frozen Phase 1
comparison separate from this cloud-only optimization comparison.
