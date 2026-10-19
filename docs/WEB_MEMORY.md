# Graphics quality and memory

The browser and native preview use `GraphicsSettings` in
`src/graphics_quality.hpp`, the same `Renderer`, and the shared C++ debug menu.
JavaScript forwards input, supplies browser capability hints, loads requested
files, and implements the graphics API bindings. It does not define another
set of presets or a second scene renderer.

| Setting | Low | Medium | High |
|---|---|---|---|
| Maximum height / pixel budget | 360p / 640×360 | 720p / 1280×720 | 1080p / 1920×1080 |
| Landscape | Static fallback | Moving terrain | Moving terrain |
| Volumetric clouds | Off; thin fallback layer | Half resolution in each dimension | Native resolution |
| Galaxy | Off | On | On |
| Giant emissions | Off | On | On |
| Maximum texture dimension | 1024 | 2048 | 4096 |
| Stars, rings, meteors | On | On | On |
| Water reflections | Off | Off | Off |
| Frame cap | 30 | 30 | 30 |

Dimensions preserve aspect and never upscale the scene budget or exceed a 4096
texture dimension. Medium cloud targets and their sky cache contain one quarter
as many pixels as the same scene with full-resolution clouds. Texture limits
select existing galaxy mip levels and reduce decoded PNG textures. Packed
terrain distance samples remain intact when reducing their resolution.

Auto chooses Low for unknown memory/core hints, reported memory at or below
4 GiB, at most four cores, or an identified software renderer. It chooses Medium
for larger budgets below 16 GiB/eight cores, and High above those thresholds.
Browser memory hints are coarse and often capped at 8 GiB; Auto deliberately
avoids a startup stress test or a claim to know free RAM/VRAM. Native preview
users opt in with `--quality auto`, or choose a named preset explicitly.

The browser requests a high-performance adapter on both graphics APIs; the
browser/OS still chooses the device. This preference does not increase the
selected quality or its memory ceilings. The corner hint and performance
readout disclose the applied C++ preset and whether the landscape is moving.
Auto Low is a reduced preview, not a fidelity comparison with the desktop's
default full renderer.

CI compares browser pixels directly with the unmodified native CLI at identical
dimensions, scene/weather clocks, route position and feature state. Daylight
includes a 960×540 fixture in addition to the small diagnostic captures. Night,
storm and cloud fixtures cover both browser APIs. The full image stays within
one average 8-bit code, with at most 0.1% of channels
differing by more than eight codes. The landscape allows two average codes
and 0.5% outliers, with detail energy within 5% of native. Mesa/SwiftShader
baseline captures measured at most 1.43 average landscape codes, 0.315%
outliers and 2.04% detail-energy difference; isolated raster/texture-filter
differences must not hide lost landscape detail. Raw captures and
`web-native-parity.json` accompany the browser artifacts. Image agreement does
not establish equivalent accelerated-GPU frame rates.

Open the corner debug menu and choose **Graphics**. The overall slider selects
a preset. Custom switches control terrain, volumetric clouds, galaxy, stars,
rings, giant emissions, meteors, and reflections. Resolution, texture size,
cloud resolution and frame cap are adjustable. Volumetric clouds require the
moving terrain/climate model. **Apply** commits the draft; dragging does not
allocate resources. Both native hosts and WASM rebuild through the same renderer.
While the browser menu is open, final presentation can use up to 1080p so bitmap
labels remain readable. The scene and cloud targets retain the selected budget;
closing the menu restores the smaller presentation. The memory readout reports
scene dimensions and includes these temporary presentation allocations.

## Evidence and changes

The preceding development-machine boot recorded a kernel `global_oom` event:
`sky-render-test` was killed with roughly **1.97 GiB anonymous RSS**. This
confirms memory exhaustion during rendering validation. It does not establish
the cause of a later machine lockup, and no lockup is deliberately reproduced.

Local headless Chromium/SwiftShader measurements used one sequential browser,
a 1100 MiB process-tree limit, 90% of one CPU, no swap, and a 768 MiB system
reserve. Aggregate peaks include Node, the browser, driver, and cache:

| Configuration | Aggregate peak | WASM capacity | Requested GPU storage | Scene asset download |
|---|---:|---:|---:|---:|
| Old build, terrain/volume/galaxy/stars disabled | 902.7 MiB | 512 MiB initial | Not instrumented | 97.8 MiB package |
| New build, isolated minimal scene with High textures | 865.3 MiB | 81.1 MiB | 97.4 MiB | 7.0 MiB |
| New Auto Low, including stars/rings, 640×360 | 524.1 MiB | 64 MiB | 13.7 MiB | 7.3 MiB |

These development measurements precede the final budget-sized display-color
target, which adds about 0.9 MiB requested GPU storage at Low. The CI matrix
records the final implementation on each backend. That target keeps terrain,
clouds and tone mapping at scene resolution before the inexpensive presentation
upscale; reduced cloud targets use normalized sampling in the shared compositor.

These are different quality configurations: the stripped diagnostics rendered at
320×180, while Auto Low rendered at 640×360. They demonstrate memory behavior,
not equal-quality speed or desktop-equivalent FPS. Individual cold starts vary;
the baseline process-tree cap was near its soft reclaim threshold. Local galaxy
isolation approached 1 GiB and became slow, so it was stopped rather than
raising the developer-machine limit. Full feature matrices run in CI.

The remaining memory is substantial: the minimal High-texture measurement
attributed approximately 244 MiB PSS to the GPU process and 207 MiB PSS to the
scene renderer process, beyond the browser/services. In Auto Low those two
figures fell to approximately 148 and 105 MiB. RSS includes shared mappings;
PSS apportions them. Neither is synonymous with explicit texture allocation.
Under the one-CPU software envelope, GPU-process CPU time dominates C++ command
encoding. Those timings must not be interpreted as accelerated-GPU performance.

The implementation addresses the measured costs:

- Only shaders are preloaded into the WASM package (about 84 KiB). Selected
  native asset groups are downloaded sequentially; disabled terrain/galaxy
  groups are never fetched. Source files are unlinked after construction.
- WASM starts at 64 MiB and grows only when needed, with a **768 MiB ceiling**.
  The debug display distinguishes live C++ malloc use from WASM capacity.
- Low avoids the moving-world and volumetric-cloud working sets. Its texture
  limits reduce the fallback's three 4096-wide maps, which otherwise account
  for most of the minimal scene's approximately 97 MiB GPU storage.
- Disabled stars skip the catalog; disabled ring/star/emission effects skip
  their shader programs. High retains the shared native rendering algorithms.
- Apply waits for the outstanding frame, deletes the old renderer, and waits
  for resource retirement before constructing a replacement. WebGPU handles
  deletion-only submissions and clears obsolete pipeline references.
- Viewport/aspect changes retire only size-dependent frame targets before
  allocating their replacements. World data, terrain meshes, shadows, programs
  and assets remain loaded. This avoids old/new target overlap near the GPU
  ceiling during fullscreen or window resizing, without repeating world builds.
- Both browser APIs track requested buffer/texture/renderbuffer storage and
  reject allocations exceeding **512 MiB before issuing them**. Driver shader
  caches, browser processes, and physical VRAM overhead are outside this ledger.
  A limit failure stops the worker and offers Low/another backend on reload.
- Rendering retains one outstanding GPU frame, suspends while hidden, and
  excludes resource loading/reconfiguration from the browser scene clock.

WASM linear memory grows but cannot shrink in place. Switching High to Low
reuses the previous heap capacity and may not reach the cold-start Low figures
above. **Stop preview**, then launch Low, to release the old worker heap.
Driver caches have their own lifetimes too; stable live GPU allocation counts
do not promise that all process RSS immediately returns to its cold-start level.

## Reproduce safely

Run lightweight checks locally inside the envelope, with an unwrapped Chromium
path and software rendering. The wrapper fails closed when the cap cannot fit
alongside the reserve or systemd cannot establish the cgroup. It records peak
memory, CPU time, exit status, and available system memory, stops on a depleted
reserve, and terminates its own process tree on timeout.

```sh
python3 scripts/browser_budget.py --report captures/budget.json -- \
  env -u DISPLAY -u WAYLAND_DISPLAY -u NIXOS_OZONE_WL \
  CHROMIUM_PATH=/path/to/unwrapped/chromium PREVIEW_BACKEND=webgl2 \
  PROFILE_STAGES=minimal,auto-low node tests/web_memory_profile.cjs
```

The PR/manual workflow builds the previous revision and the candidate. Separate
headless browser processes isolate minimal, stars, galaxy, terrain, clouds, and
complete scenes on WebGL 2 and WebGPU; the candidate also checks Auto Low. CI
uses a 3 GiB process-tree cap and two CPU equivalents, and verifies the kernel
ceiling with an isolated oversized-allocation test. The ceiling self-test puts
its soft threshold at the same 128 MiB
hard limit so reclaim throttling cannot hide an OOM kill behind a timeout;
normal profiling retains the 82% soft threshold and the same hard caps.
Every feature stage has a
deadline. `bounded-memory-profiles` contains raw samples, per-process RSS/PSS and
CPU time, source/heap/GPU counters, and a Markdown comparison. No full-scene
stress or hardware-GPU test runs on the development desktop.

`scripts/summarize_web_memory.py` checks that every stage completed, consumed
files were released, unused groups stayed absent from the minimal scene, and
heap/GPU allocations stayed inside their ceilings. Playwright also repeatedly
applies custom settings and checks that live GPU resource sizes return to the
same budget. Full native rendering tests preserve the existing fidelity gate.
