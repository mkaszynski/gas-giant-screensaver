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
- Work in progress: broader landforms, shores/water detail and final visual,
  performance, memory, native and CI acceptance are outstanding.
