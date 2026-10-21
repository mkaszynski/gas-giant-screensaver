# Browser graphics backends

The browser preview prefers **WebGPU**, and falls back to **WebGL 2** when the
browser cannot provide a WebGPU adapter/device. The Graphics selector before
launch, or `?backend=webgpu` / `?backend=webgl2`, forces either backend. A forced
WebGPU launch reports unsupported hardware instead of silently choosing WebGL.
The footer and `window.previewMetrics.backend` report the actual backend.
Shader, deployment and rendering errors are reported rather than hidden by fallback.

Both paths execute the shared C++ simulation, terrain worker and render passes.
The native GLES renderer and lock screen are unchanged. WebGPU uses a small
browser command adapter for the renderer's existing GLES subset; it **does not
create a WebGL context or copy a WebGL-rendered image into WebGPU**. Unsupported
operations fail explicitly. This adapter is specific to this renderer, not a
general WebGL implementation.

## Shaders and resources

At build time, `scripts/build_webgpu_shaders.py` expands the existing GLSL and
its program variants, splits combined image/sampler bindings and std140 uniform
blocks, and uses glslang, SPIRV-Cross reflection and Tint through twgsl to emit
WGSL. The browser receives a roughly 0.7 MB manifest, **not a shader compiler**.
Original source fingerprints identify each program; changed/unbaked source
fails at linking instead of selecting stale shaders. Tests compare the Python
and JavaScript fingerprints and validate every WGSL module in Chromium.
The manifest includes the shared meteor optical kernel and its HDR scaling;
integrating the native meteor renderer must also retain this shader variant.
It bakes 32 programs, covering both fixed cloud profiles, general/deep-water/
inland/reflected terrain, both composite variants, rain density and the sky-only
and empty-volume fragment cloud specializations, plus calm final/sky variants.
See [cloud shader and binding optimization](WEBGPU_CLOUD_COST.md). Material defines precede
the cloud define, matching the shared C++ source fingerprints exactly. Composite
variants retain the swapchain coordinate conversion regardless of their label.

The build-only twgsl JS/WASM files are pinned to a Babylon.js commit with SHA-256
checksums verified before use. CMake downloads missing tools into the build tree;
`OBSERVATORY_TWGSL_DIR` can point to an already populated cache for offline builds.
No new runtime CDN is required. The pinned writer's redundant legacy 16-byte
strides on naturally aligned vec4 and mat4 arrays are removed. Uniform vec2
arrays use padded vec4 storage, with `.xy` reads and a separate logical upload
type; their original 16-byte std140 element stride is preserved. Other unexpected
legacy strides fail the build.
The existing GLSL derivative behavior is retained with WGSL's
`derivative_uniformity` diagnostic disabled, followed by browser shader validation.

The adapter preserves HDR, native anisotropic terrain filtering, comparison-filtered
terrain shadows and four-sample MSAA on both terrain color/coverage and distance. It remaps clip depth and
render-target coordinates without changing the shared lighting calculations.
Star point sprites become instanced quads with the same pixel-integrated optical
profile. Cached cloud-light slices render directly into the 3D texture through
WebGPU color-attachment depth slices, without a staging texture or copy.
Sampled/storage noise volumes omit render-target usage; this also avoids an
invalid lazy-clear view in older Dawn/Chromium versions. 2D and 3D mipmaps are generated on the GPU.
Floating-point cloud noise avoids a synchronous readback precision probe, which WebGPU does not
support. BC6H sky compression is used when the adapter supports BC textures;
the existing HDR decoder handles other devices.

At most two submissions are outstanding. CPU encoding can overlap the previous
GPU frame, rather than adding CPU and GPU costs in series. Both frames reuse the
same targets and 4 MiB GPU uniform arena: queue-ordered
[`writeBuffer`](https://www.w3.org/TR/webgpu/#dom-gpuqueue-writebuffer) uploads
capture each frame before its commands. Submission immediately resets CPU
offsets; an older completion never changes a newer frame's offsets. Quality and
size changes drain all submissions before retiring resources. Dynamic offsets
and cached bind groups avoid allocating a uniform buffer per draw.
Pipeline caches are capped at 256 entries, texture bind groups at 64
per program. Resources replaced during a frame retire after queue completion.
Both browser backends request a high-performance adapter; the browser and OS
choose the actual device. Hidden tabs suspend rendering. The shared C++ presets
set resolution, texture quality and features, with High targeting 1080p / 30 fps.
Auto can choose a substantially reduced scene; the corner hint identifies the
applied quality and moving/static landscape. See [quality policy](WEB_MEMORY.md).

## Validation and limits

The shader conversion tests require Node and `glslangValidator` on `PATH`.

```sh
python3 tests/test_webgpu_shaders.py
node tests/webgpu_data.cjs
PREVIEW_BACKEND=webgl2 npm test --prefix web
PREVIEW_BACKEND=webgpu npm test --prefix web
node tests/web_backend_parity.cjs
```

Browser checks run the actual static WASM build below the repository subpath,
including isolation/reload, shader validation, pause/resume, time seeking,
resize, terrain streaming, stop/relaunch, automatic fallback and day/dusk/night/storm
image comparisons. A stars-disabled eclipse comparison checks that the point
sprites contribute visible light on both backends. The eclipse fixture also
regresses scissor-disable state and filtered mountain occlusion: either can
otherwise clip the aurora or expose a bright strip of rings through the ridge.
`surface=0`, `stars=0` and `galaxy=0` mirror existing native review options.
`volumetric=0` selects the existing native `--no-weather` thin-cloud comparison.
The day, dusk, night, storm and thin-cloud fixtures compare both browser backends;
the thin-cloud case exercises the padded cloud-offset array in a live pipeline.
CI also compares both browser APIs directly with the full native renderer at
identical scene settings and physical dimensions, including a 960×540 daylight
capture. A separate landscape-region check catches differences hidden by the
large sky region. Captures avoid browser/DPR resampling.
Software GPU checks establish behavior and visual agreement, **not hardware performance**.
Normal `cost` metrics measure CPU command submission; callbacks wait for GPU
completion for metrics and bounded queue backpressure. Synchronous `benchmark=1` remains
available with `backend=webgl2`; WebGPU reports an explicit error for that mode
because it cannot implement a synchronous `glFinish` wait. Chromium is validated
locally and in CI; Safari and Firefox hardware testing remains future work.
See the [recorded browser comparison](webgpu-validation.json).

Primary references:

- [WebGPU specification](https://www.w3.org/TR/webgpu/)
- [WebGPU 3D slice attachments](https://developer.mozilla.org/en-US/docs/Web/API/GPUCommandEncoder/beginRenderPass)
- [WGSL specification](https://www.w3.org/TR/WGSL/)
- [Emscripten WebGL support](https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html)
- [Tint / twgsl](https://github.com/BabylonJS/twgsl)
