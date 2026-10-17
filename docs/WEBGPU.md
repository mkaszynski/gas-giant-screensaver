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

The build-only twgsl JS/WASM files are pinned to a Babylon.js commit with SHA-256
checksums verified before use. CMake downloads missing tools into the build tree;
`OBSERVATORY_TWGSL_DIR` can point to an already populated cache for offline builds.
No new runtime CDN is required. The pinned writer's redundant legacy 16-byte
stride on vec4 arrays is removed; other unexpected legacy strides fail the build.
The existing GLSL derivative behavior is retained with WGSL's
`derivative_uniformity` diagnostic disabled, followed by browser shader validation.

The adapter preserves HDR, native anisotropic terrain filtering, comparison-filtered
terrain shadows and four-sample MSAA on both terrain color/coverage and distance. It remaps clip depth and
render-target coordinates without changing the shared lighting calculations.
Star point sprites become instanced quads with the same pixel-integrated optical
profile. Cached cloud-light slices render directly into the 3D texture through
WebGPU color-attachment depth slices, without a staging texture or copy. 2D and 3D mipmaps are generated on the GPU.
Floating-point cloud noise avoids a synchronous readback precision probe, which WebGPU does not
support. BC6H sky compression is used when the adapter supports BC textures;
the existing HDR decoder handles other devices.

Only one frame is outstanding. A 4 MiB uniform arena preserves each draw's
state; dynamic offsets and cached bind groups avoid allocating a uniform buffer
per draw. Pipeline caches are capped at 256 entries, texture bind groups at 64
per program. Resources replaced during a frame retire after queue completion.
The browser requests a low-power adapter and keeps the existing explicit
1080p / 30 fps target and hidden-tab suspension. No automatic resolution or
material-quality reduction was added.

## Validation and limits

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
Software GPU checks establish behavior and visual agreement, **not hardware performance**.
Normal `cost` metrics measure CPU command submission; callbacks wait for GPU
completion before another frame is submitted. Synchronous `benchmark=1` remains
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
