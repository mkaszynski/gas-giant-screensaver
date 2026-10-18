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

## Preserving raster sampling

Recomputing a fragment's UV by division in a compute shader can differ from the
rasterizer's interpolation by one float ULP. At an exact sky-cache cell boundary,
that can select a different set of neighbors and change the cache's reuse
decision. An initial implementation exposed three pixels beyond the two-code
image limit at 1920×1080.

Two floating-point strips capture the rasterizer's coordinates with the original
fullscreen vertex shader and viewport. One row stores U; one column stores V.
Compute fetches those coordinates, preserving the original decisions without
storing a full-screen coordinate image. Storage is 48,000 bytes at 1920×1080;
generation requires two small draws only on first use and resize.

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
preservation of nondefault host image bindings.

The first native hardware run passed with differences of zero or one code.
Full software-renderer, build, browser, Nix, CI and controlled performance gates
are still required before release. Exploratory prototype timings on a shared
desktop reduced cloud time from about 20 ms to 16 ms; those measurements do not
establish a sustained 30 fps result for the complete renderer.

For a direct comparison, use identical dimensions, scene/route/weather clocks,
and `--benchmark` settings, adding `--no-weather-compute` for fragment execution.
The benchmark reports the selected cloud backend. Keep the frozen Phase 1
comparison separate from this cloud-only optimization comparison.
