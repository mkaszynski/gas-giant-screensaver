# Landscape fidelity, Phase 2

Approved target: 30 fps at 1920×1080 on the development computer. 60 fps is a
later goal. Complete renderer cost must stay within 1.30× the frozen Phase 1
baseline, including sky, celestial bodies, clouds, postprocessing and terrain.
The deadline is 33.33 ms; average FPS alone is insufficient. Rendering timings
and native presentation cadence are separate acceptance checks.

Baseline: `f882d4d701308b06c25be0a2118871dc244f9794`, packaged binary and assets
from `/nix/store/xmqsrin8q98h09i7djv3dvf0d4ivyi3v-gas-giant-screensaver-0.1.0`.
[Raw repeated baseline measurements](landscape-phase2-baseline.json) record
binary and input hashes. Do not replace this baseline when optimizations land.

## Implementation order

1. Record frame tails, shadow draws and streaming events. Redistribute terrain
   geometry into the visible view and spread replacement uploads over frames.
2. Improve one forested mountain reference scene with physically based, filtered
   materials, coherent landform masks, depth in crowns and calibrated lighting.
3. Extend landforms/materials across rocky uplands, grasslands, drylands, tundra,
   snow and coasts. Offline numerical caches replace repeated runtime generation.
4. Improve water waves, shore depth/wetness and reflected sky/clouds. Test coarse
   terrain reflections; keep them only if stable and within budget.
5. Improve thin clouds and their consistent lighting/shadows/reflections without
   adding a full volumetric ray marcher.
6. Review daylight, sunset, night and moving captures; validate hardware and Mesa
   software rendering, loop/altitude/occlusion/antialiasing/shadow continuity,
   bounded memory, native Hyprlock and matched repeated performance runs.

Use `feat/landscape-fidelity`, based on accepted Phase 1 (its source PR is still
open). Checkpoint tested work; open a stacked feature PR and require passing CI.
Operator review precedes merging or changing the NixOS source pin. Preserve the
72-hour route, smooth airplane flight, sky clocks and astronomical rendering.

## Progress

- Baseline frozen: three alternating 180-frame runs each for ocean, forest and
  snow at the existing 30 fps cap, using immutable packaged assets.
- Geometry checkpoint `5c92b4e`: 279,072 vertices, 1,671,168 indices; terrain
  replacements upload at most 2 MiB per normal frame into an inactive buffer,
  switching mesh/bounds/shadow frames together only when complete.
- Reference materials: two six-layer 512² arrays (~16 MiB with mips), linear
  albedo/perceptual roughness and numerical normal/cavity fields. GGX dielectric
  sunlight and normal-variance roughness filtering replace matte-only lighting.
  Skylight uses a two-direction approximation, not full irradiance convolution.
- Forests: overlapping lobed broadleaf and tapered conifer height/normal fields,
  height-derived occlusion and bounded leaf lighting. An eight-direction crown
  visibility cache at four solar elevations (~10.7 MiB with mips) filters shadows
  after occlusion so they survive airplane-distance mips. Comparison renders
  measure a 25-code sunset difference; it costs two forest-only filtered lookups.
- Clouds: shared angle-dependent thin-sheet opacity, coarse self attenuation,
  forward brightening and atmospheric twilight color; solar halo, shadows and
  reflected clouds use the same coverage field.
- Native timer accounts for CPU render work instead of delaying a full interval
  after it. Actual native presentation cadence remains to be measured.
- Focused checks pass: 38 CPU/CLI tests, 10 world bake checks, 5 physical material
  checks, hardware surface and predicted-shadow tests, and real benchmark CSV
  output. Full hardware/software gate and packaged native verification pending.
- Initial combined forest probe: 10.61 ms median / 16.05 ms p95; one 48.54 ms
  GPU stall after startup means the 30 fps acceptance is not yet established.
  These development probes are not the final paired performance comparison.
- Three coherent landform types now blend geometry and residual lighting. The
  regenerated flight cache is bound to their hashes and passed 2,097,152 altitude
  samples: minimum clearance 620 m and maximum climb/descent 3.32672 m/s.
- Water: four footprint-filtered wave scales, deep-water dispersion, filtered
  GGX solar glints, depth-dependent coastal color and wet shore materials. A
  quarter-resolution coastal terrain reflection prototype reuses resident geometry
  (64,000 triangles) and cached shadows; it skips pure-ocean/inland views.
  Reflections overscan and fade boundaries; cloud shadows use the virtual
  observer position, while aerial light integrates the real reflected path; tiny-step water tests change at most
  one display code. Retention depends on the paired rendering-cost measurements.
- Reference checkpoint `091c3cc`: full local hardware/software suites passed;
  Nix preview/native builds and push/PR CI passed. Isolated native rendering,
  cyan input and unlock transition passed with roughly 33 ms timer cadence.
- New complete-renderer paired performance, final native build, memory checks,
  review captures and CI acceptance are still in progress.

### Cost tuning in progress

Initial paired complete-renderer runs exceeded the allowed increase at coasts.
The reflection target was reduced from half to quarter resolution (roughly
1.5 MiB of color/depth storage at 1080p). Clear-cloud pixels skip invisible
lighting, and negligible water glints retain their BRDF tail without five
unneeded cloud-shadow lookups. A quad-coherent postprocessing shortcut skips
sky/cloud/halo work behind fully opaque terrain; comparison renders prove it is
pixel-identical to the full sky path. Partial silhouettes retain the full pass.
The numerical fields and physical water glints remain enabled.

The installed lock screen began sharing the GPU during performance tuning.
Final acceptance requires an unlocked desktop, paired clean measurements and
30 fps deadline checks; timings from the competing-renderer runs are diagnostic.

### Terrain resolution refinement

The finer beta mesh has 512,800 vertices and 3,072,000 indices. The visible
1.5–45 km slopes receive almost all radial samples, doubling radial resolution
near 3.5 km; angular sampling improves 1.5×. Full measured 16 m relief normals
retain small gullies independently of the geometry LOD, using the existing
texture array. Continental/canopy normals exclude measured slopes to avoid
counting them twice. Camera/shadow block culling is pixel-identical to drawing
the full mesh in daylight, sunset and night on hardware and Mesa.

Warm deep water uses coarser interior fans, retaining every fine boundary edge;
the outer horizon band stays fine. Day/night ocean/coast comparisons differ by
at most one display code, with an identical watertight boundary. Land geometry
and shadow casters retain the detailed mesh. The coastal terrain reflection
prototype exceeds the budget and is now opt-in; sky/celestial/cloud reflections
remain in the standard profile. Repeated whole-renderer budget measurements and
final package/CI validation of this refinement are in progress.

The tuned profile passes the **median** 30% allowance in all four fixtures
(4.3% ocean, 18.6% forest, 7.0% snow, 24.6% coast). It does **not** pass the p95
allowance or zero steady-frame deadline misses on the shared desktop. The full
report and slow runs are retained in `landscape-phase2-resolution-benchmark.json`;
see `VALIDATION.md`. Release performance acceptance remains open. The latest
local gate passed 40 CPU/CLI checks, 38 offline tests, all 13 GPU suites on AMD
hardware and Mesa, and the real benchmark CSV checks on both renderers.

## Acceptance audit, 19 October 2026

The preceding checkpoints are historical. The landscape implementation was
reviewed and merged through PR #12; subsequent weather and rendering work does
not replace the frozen Phase 1 reference above. Its packaged executable still
matches the recorded SHA-256 `776dde86a09f15a10a9e02a64f1b08d5e14e7592c97d6aafb9eec655e5a02865`.
The active completion gates remain stricter than an average-FPS demonstration.

| Requirement | Current evidence | Remaining verification |
| --- | --- | --- |
| Coherent detailed geometry, filtered PBR materials, dense canopy and water | `surface.cpp`, numerical world/material caches, `fidelity-render-tests`, `water-render-tests` and offline world/material tests; operator reviewed the landscape | Preserve these effects and their output through further optimization |
| Hardware/software correctness | Application `5bf5c20` passes all renderer/Nix/browser/memory CI; all 45 compute-reference and 30 specialization software images are identical; target GPU flights and a native preview work | Verify later product changes and browser hardware performance |
| 30 fps at native 1920×1080 | Four current forest/coast flights pass their workload controls with zero missed deadlines in 2,840 steady frames; largest p95 27.655 ms; native preview shows 30.0 fps | [Browser hardware diagnostic](BROWSER_GPU_TIMING.md): 20.76 fps, below target; the original full comparative trial remains invalid after its reference-only collection failure |
| At most 30% added complete-renderer cost | Fresh original frozen Phase 1 comparison passes all 12 runs and six matched pairs; worst median ratio 1.093, p95 ratio 1.058 | Preserve original baseline, complete-renderer scope, cloud isolation, controls and 1.30 limit through later changes |
| Bounded memory and streaming | Every current long flight completes four mesh revisions, with normal uploads at most 2 MiB; whole-flight process-tree peak 374.7 MiB; browser memory CI passes | Continue distinguishing requested GPU storage, WASM capacity and process-tree RAM |
| Native integration and operator review | Current application passes all four Nix checks and a target-GPU preview; the operator authorized incremental landing of PR #29 and #30 on 20 October 2026 | Browser performance acceptance remains open; the installation pin is unchanged |

Neither green correctness CI nor the previous median cost pass establishes full
performance acceptance. Do not mark Phase 2 complete while these gates remain
open. Failed timing controls and rejected optimization experiments are retained
with the local experiment records rather than substituted with faster retries.
The [current validation report](BENCHMARK_CPU_BUDGET.md) publishes numerical
results, frame traces, source/binary fingerprints and the retained failed
reference. Collector CPU headroom uses stricter child limits inside the same
parent resource envelope; it does not relax any workload or cost threshold.
