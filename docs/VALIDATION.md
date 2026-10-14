# Validation

## Landscape fidelity beta

The refinement on `feat/landscape-fidelity` targets 30 fps at 1080p, with a
30% complete-renderer cost allowance against frozen Phase 1. It remains a beta;
operator review and frame-tail acceptance precede merging or changing the NixOS
source pin. The complete measurement is retained in
[landscape-phase2-resolution-benchmark.json](landscape-phase2-resolution-benchmark.json).

The visible mesh has 512,800 vertices with 1.5× angular and roughly 2× near-view
radial resolution versus the earlier beta. Independent triangle-interior checks
use the cached elevation field, rather than merely counting vertices:

| Height fixture | Earlier beta mean error | Refined mean error |
|---|---:|---:|
| Dryland | 5.087 m | 2.259 m |
| Forest | 2.537 m | 1.134 m |
| Coast | 0.076 m | 0.044 m |

Full cached relief gradients give small gullies and cliffs consistent lighting
without duplicating the mesh slope. Coarse warm-water interiors retain every
fine boundary edge. Ocean/coast day/night comparison images differ by at most
one display code; land and the horizon band retain fine geometry. Frustum
culling preserves every pixel in day/sunset/night hardware and Mesa tests.

Three alternating 180-frame runs per scene at 1920×1080 / 30 fps measured the
following medians of whole-renderer run summaries on AMD Renoir:

| Scene | Phase 1 median | Beta median | Added cost | Beta p95 |
|---|---:|---:|---:|---:|
| Ocean | 11.652 ms | 12.155 ms | 4.3% | 26.540 ms |
| Forest | 11.025 ms | 13.076 ms | 18.6% | 22.066 ms |
| Snow | 10.859 ms | 11.620 ms | 7.0% | 20.010 ms |
| Coast | 12.118 ms | 15.102 ms | 24.6% | 36.024 ms |

The median cost allowance passes. The p95 allowance and zero-missed-deadline
requirement **do not pass** on the shared desktop: the candidate recorded
26 / 12 / 5 / 25 steady frames over 33.33 ms across the three ocean / forest /
snow / coast runs, excluding the first ten startup frames in each. One ocean
run had a 28.57 ms median; it remains in the raw report. These figures do not
establish an uninterrupted 30 fps guarantee. Do not discard the slow runs or
replace the frozen baseline. Coastal terrain reflections were over budget and
are opt-in (`--terrain-reflections`) in the refined profile.

The local gate contains 40 CPU/CLI checks, 38 offline model/cache tests, and 13
GPU executables exercised on hardware and Mesa. The actual CSV benchmark is
also tested for bounded uploads and file failures. Existing astronomical,
opacity, antialiasing, cloud, shadow, debug and route-loop checks remain in the
gate. The source PR stays draft while performance acceptance is outstanding.

## Three-day airborne biomes (Phase 1)

The default scene is the 72-hour flight documented in [SURFACE.md](SURFACE.md).
Historical static captures below use `--fixed-view`. Moving latitude/longitude
changes sky orientation and local eclipse times.

The CPU/CLI gate contains 34 checks. An 8,192-position route sweep at both small
and Unix-scale sky times checks closure, six climate categories, north/south
latitude range, shared terrain/celestial altitude, ground clearance and bounded
vertical speed. The route has no terrain-dependent lateral offset. Camera
bearing is independent of accelerated orbital libration, and east follows the
physical moon rotation. The minimum sampled giant center elevation is 16.11
degrees. An independent 128-position / 128-phase libration sweep keeps its upper
limb above 35.66 degrees on the mean subplanet bearing.

The offline flight builder checked 2,097,152 positions. Minimum ground clearance
was **620 m**, maximum climb/descent **3.32672 m/s**, and maximum sampled ridge
on the preserved bearing **0.38599 degrees** (below its 28-degree safety limit).
A 128 KiB periodic B-spline cache replaces the old 2.1 MiB lateral-detour cache.
Runtime performs no route planning or horizon scanning.

The moving GPU suite checks opaque terrain coverage, fractional silhouettes,
no upper-sky crop rectangle, celestial occlusion, identical-phase loop images,
actual flight motion, mesh replacement/fresh geometry agreement, ground shadows
from the visible cloud field and host GL state restoration. The source hardware
check measured **0 codes** maximum at identical loop phases and **0.00462 codes**
mean difference between streamed and fresh geometry. It also verifies clouds
actually appear in ocean reflections. It checks parallax over
ten seconds at airplane height rather than the old one-second close-ground view.

The 28 offline model checks include preserved broad landforms, relief normals
and valley occlusion, deterministic canopy fields, packed normal bounds,
dense overlapping crown coverage and source-bound cache hashes. Conservative crop bounds contain sampled geometry;
a 1080p forest comparison before/after this CPU optimization was pixel-identical.
The GPU gate also checks that planetshine visibly illuminates nighttime land. The original
fixed-landscape and astronomical tests remain in the full gate.

Three alternating 180-frame runs per biome at 1920x1080 / 30 fps on AMD Renoir
measured the following medians of run summaries (milliseconds):

| Biome | GPU median | GPU p95 | CPU+GPU median | CPU+GPU p95 |
|---|---:|---:|---:|---:|
| Ocean | 9.62 | 13.78 | 11.62 | 16.94 |
| Forest | 8.87 | 12.92 | 10.93 | 15.89 |
| Snow | 8.92 | 12.53 | 10.86 | 14.80 |

Raw repetitions are in [surface-benchmark.json](surface-benchmark.json); every
run is retained. The [preceding relief version](surface-pre-landform.json)
measured GPU medians of 10.22 / 8.50 / 8.21 ms for ocean / forest / snow,
including high first-round forest/snow outliers. These are
shared-desktop timings, not watts or universal frame-time guarantees. The
[earlier shadow/crop measurements](surface-shadow-before.json) recorded roughly
14–19 ms synchronized medians. Cached block bounds eliminate most repeated CPU
projection work; removing invisible close trees and adapting shadow cadence
reduce wasted work. The original smaller-mesh streaming measurements remain
as historical diagnostics in [surface-streaming-before.json](surface-streaming-before.json).

The final Nix preview and native Hyprlock widget build and pass configuration
checks. An isolated compositor run verified actual rendering, cyan input and
unlock without locking the user's desktop. Review images in [SURFACE.md](SURFACE.md)
come from this package. Biome art remains subject to operator visual review;
water/hydrology and silhouette limits are documented there.

The preview-only debug slider has a deterministic clock test covering pause,
seek, range changes, endpoint clamping, deferred drag commits, Unix-scale time
and coupled flight/weather offsets. A separate GPU suite checks actual slider
coverage, unmodified upper-sky pixels, 2x display density and GL state
restoration on hardware and software rendering. An isolated Xvfb run of the
packaged interactive preview exercised short mouse clicks, pause/resume,
day/year range switches, drag previews, actual sky/route seeks and reset.
CLI checks reject combining
the interactive overlay with hidden capture, recording or benchmark modes.

A separate linear-visibility GPU fixture checks six predicted-shadow rollovers
at grazing sunlight. The largest change across 0.2 ms around a rollover is
**1/255** on both AMD and llvmpipe. Disabling temporal interpolation in a
reference fixture jumps by **120/255 (AMD) / 119/255 (software)**, proving the
test detects discrete cache updates. Shadows still move substantially over two
seconds. Tests verify four initial depth draws, zero draws during interpolation,
one hidden-map draw at each staggered rollover, and identical reconstruction
after rewinding. The normal direct-light shader is not advanced ahead of time.

## Relightable terrain

The foreground now uses measured 2m Swiss relief with modeled rock, vegetation,
conifer crowns and an alpine lake, rather than the old flat mountain panorama.
The fixed-view cache supports directional relighting, eight-angle horizon
shadows, changing aerial perspective, moving wind waves and reflections of
terrain, clouds and visible HDR scene radiance. Native sky and body simulation
remain unchanged; viewing elevation decreases from 28 to 26 degrees.

All 21 CPU/CLI checks, 11 galaxy model tests, eight terrain model tests and eight
GPU suites pass locally. Dry opaque interiors are compared against extreme
sky/glare and celestial toggles; lake reflection is intentionally tested
separately. A software-renderer regression exposed a shoreline threshold that jumped by
62 display codes at Unix-scale weather time. Coverage blending fixes it;
strict continuity checks remain bounded to two codes on both AMD and llvmpipe,
including the true plus/minus-pi phase wrap.
The new GPU suite verifies directional light response, actual
visible lake coverage, moving reflected images, phase continuity at wraps and
Unix timestamps, and absence of the former cache-boundary arc. The galaxy
regression still detects 401 visible eclipse stars (183/255 peak contrast).

Three alternating 180-frame runs per variant at 1920x1080 measured total GPU
medians of **5.235 / 5.833 ms** in daylight (+11.4%) and **4.885 / 5.472 ms**
at night (+12.0%). The median of run p95 values is 7.811 / 6.474 ms in daylight
and 5.559 / 6.049 ms at night. These include the camera adjustment and are
shared-desktop measurements, not watt readings or universal driver guarantees.
Raw repetitions are in [terrain-benchmark.json](terrain-benchmark.json).

The Nix-packaged 1080p daytime capture is pixel-identical to the source capture.
Actual captures and source/renderer limits are documented in
[the terrain design](TERRAIN.md).


## Rendered HDR galaxy

The current beta replaces the flattened star map with deterministic stellar-
emissivity integration through measured CSFD and local Edenhofer 3D extinction.
It retains the same 8,920 cached HYG stars, but treats unresolved light as a
continuous radiance field. Six 2048-pixel BC6H faces and complete HDR mipmaps
use **32 MiB**, versus 128 MiB for the preceding photographic cube. Original
8-bit/BC7 assets and their image-conversion pipeline are removed.

All **21 CPU/CLI checks, 11 offline model tests and seven GPU suites** pass on
the local AMD GPU. The diffuse 1080p Galactic-center variation proxy is
**0.01959**, versus 0.49425 for the unfiltered raster and 0.22015 after its mip
bias: approximately **96% / 91% lower**, respectively. This is a normalized
pixel-scale variation measure, not a claim of exactly reproducing every measured
stellar association. Band/pole contrast remains **8.25x**. The eclipse regression
detects **400 visible catalog stars**, with 195/255 peak contrast.

The data check found 496 exact-zero conversion-seam dust entries. Treating them
as transparent produced false bright patches; deterministic neighboring-column
reconstruction removes them. A regression preserves measured nonzero values and
rejects holes with no valid neighbors. The model tests also check coordinate
alignment, distance/magnitude selection, deterministic output, quadrature
convergence, spectral reddening, cumulative dust budgets and HDR mip averages.

The existing RGBA16F scene now uses a reversible 4096x storage scale, without a
new target or pass. A dim 2048-pixel test gradient retains **1024 distinct levels**
with **0.091%** worst relative error. Explicit high-precision atmospheric sampling
avoids horizontal bands exposed by the new continuous sky; its test gradient
retains **984 levels**. Solar clipping remains far above display white, and all
solar, body, ring, atmosphere, emission and foreground regressions pass.

Uncompressed RGB16F model references were rendered through the same GLES path.
Compared with BC6H, normal-exposure output differs by **0.0035/255 on average**,
with **1/255 maximum**. At 10x and 100x exposure, mean error is **0.0178/255** and
**0.0314/255**, respectively; their 99.9th-percentile error is 1/255 and maxima
4/255 and 3/255. These elevated exposures are stress tests, not the normal preset.
Portable and GPU BC6H decoding produce zero display difference in the tested
eclipse. Raw comparisons are in
[galaxy-model-image-quality.json](galaxy-model-image-quality.json).

Each performance value below is the median of three alternating 180-frame runs,
after 40 warmup frames. Baseline is the original starfield at `8d50bba` with
profiling only. Both median and p95 stay within the +20% combined-sky budget.
The instrumented benchmark is separate from production. These are measurements
on a shared desktop, not watt readings or guarantees for every graphics driver.

| Scene | Baseline sky median | Model sky median | Change |
|---|---:|---:|---:|
| Day, 1080p | 0.791 ms | 0.695 ms | -12.1% |
| Planetlit night, 1080p | 0.795 ms | 0.832 ms | +4.6% |
| Eclipse, 1080p | 0.817 ms | 0.866 ms | +6.0% |
| Eclipse, 4K | 3.365 ms | 3.583 ms | +6.5% |

Raw medians, p95 values and individual runs are in
[galaxy-model-benchmark.json](galaxy-model-benchmark.json).

Review images are actual framebuffer captures:

- [Normal fixed-camera eclipse](images/galaxy-eclipse.png).
- [Planetlit night](images/galaxy-night.png).
- [Galactic-center diagnostic camera](images/galaxy-model-reference.png).
- [The same diagnostic camera at 10x exposure](images/galaxy-model-exposure-10x.png).

The diagnostic points toward the Milky Way and omits celestial bodies to make
sky quality visible; it is not the normal screensaver camera. Population spectra,
bar luminosity and distant dust are model approximations. Fine stellar
associations and the Magellanic Clouds from the former raster are not reconstructed
in this version. See [design and limits](GALAXY.md).

## Historical photographic sky refinement

The subsequent grain refinement widens only the diffuse map's sampling
footprint by 1.52x (+0.6 mip bias), using the existing linear-light mipmaps and
the same single lookup. In an isolated 1080p Galactic-center view, normalized
pixel-scale variation drops **55.5%**, while the mean absolute difference of
32-pixel cell averages is **0.133%** of the original total radiance. Tests also
require retained fine structure and mean brightness within 1%. In the actual
eclipse-sky review patch, pixel-scale variation drops **40.2%**. Turning off
display dithering alone reduced it only 3.2%; dithering remains to prevent
color banding. Catalog point profiles and GPU memory are unchanged. The updated
eclipse regression detects **359 visible catalog stars**, peak 188/255.
See the [before/after crop](images/galaxy-detail-before-after.png).

Three alternating 180-frame timing runs per variant measured combined sky
medians of **0.850 ms before / 0.843 ms after** at 1080p, and **3.535 / 3.544 ms**
at 4K: changes of -0.82% and +0.25%, respectively. This refinement adds no
measurable rendering burden in these shared-desktop samples. Raw repetitions
are in [galaxy-grain-benchmark.json](galaxy-grain-benchmark.json).

### Preceding high-resolution revision

The quality revision replaces the softened 4k/1024-face map with the 16k NASA
source and 4096-pixel faces, removes Gaussian preblur, restores the 16-fold
source texel-area brightness scaling, and increases catalog point exposure 10x.
That revision's eclipse regression measured **358 distinct catalog stars** above
12/255 contrast in the visible upper sky, with a peak difference of 187/255.
It tests the actual atmosphere, diffuse galaxy, occlusion and final tone mapping.
An isolated 1080p diffuse-sky check requires retained fine structure rather than
accepting a bright but blurred band. Colors, magnitudes and subpixel flux
stability remain independently tested.

BC7 sRGB compression retains the 4096-pixel resolution and full linear-light
mip chain while reducing static sky GPU storage from **512 MiB to 128 MiB**.
The actual AMD GPU supports the format. No codec runs during animation; the
hardware samples the compressed texture directly. The portable decoder path
produces **zero final-display difference** in the tested eclipse scene. GPU and
decoded uploads are compared explicitly, and the binary loader rejects incorrect
dimensions, mip sizes, truncation and trailing data.

Compared with the uncompressed source capture, the final 1080p eclipse changes
by **0.044/255 on average**, **1/255 at the 99.9th percentile**, and **3/255 at
the worst channel**. Base-level source-face comparison measures 47.48 dB PSNR
and 0.72/255 mean absolute error. These are image-fidelity checks, not an assertion
of calibrated human-eye brightness. The offline conversion and encoding tools
are not runtime dependencies. The 21 CPU/CLI checks and all seven GPU suites
pass locally. The initial timing data below belongs to the preceding softer
beta; broader tuning follows appearance review.

A 180-frame check of this quality revision measured a **0.905 ms sky median**
at 1080p eclipse, versus the initial baseline's **0.793 ms** (+14.0%). Full-scene
GPU median was **4.50 ms**; the earlier baseline sample was **4.53 ms**. This is
a spot check on a shared desktop, not the previous three-run performance gate
or a guarantee across resolutions and drivers.

## Earth sky beta

The galaxy beta has 20 CPU/CLI tests and seven GPU behavior suites, including
new catalog and sky tests. The new checks verify named J2000 star positions
(Sirius, Rigel, Betelgeuse), V-magnitude flux ratios, B-V color differences,
malformed catalog rejection, stable integrated star flux across fractional pixel
movement and 4x linear resolution, continuous cube edges, galaxy/pole contrast,
daylight washout and mountain occlusion. A solar regression compares the optimized
cosine-bound disk against the original ungated angular calculation. Both sky
layers can be disabled independently for review.

The offline bake was repeated from the pinned source checksums. All catalog
bytes and all six cubemap pixel arrays reproduced exactly. NASA's celestial
orientation and the requested PyVista reference faces were inspected; the
reference's different JPEG exposure is not used as a naked-eye brightness scale.
Actual 1920x1080 eclipse and planetlit-night captures were reviewed.

On AMD Radeon integrated graphics (radeonsi/renoir/ACO, kernel 6.18.6), each
measurement below is the median of three 180-frame runs after 40 warmup frames.
Baseline and beta were alternated on the same desktop. Baseline is `8d50bba`
with timer instrumentation only. Production builds do not include those calls.

| Scene | Baseline sky median / p95 | Beta sky median / p95 | Median change |
|---|---:|---:|---:|
| Day, time 0, 1080p | 0.760 / 0.803 ms | 0.688 / 0.717 ms | -9.5% |
| Planetlit night, time 900, 1080p | 0.761 / 0.795 ms | 0.833 / 0.877 ms | +9.5% |
| Eclipse, time 437625, 1080p | 0.793 / 0.833 ms | 0.881 / 0.906 ms | +11.1% |
| Eclipse, time 437625, 4K | 3.329 / 3.441 ms | 3.622 / 3.774 ms | +8.8% |

**Sky means background plus point stars**, where the diffuse galaxy is added.
Both median and p95 stay below +20% in these samples. Separately, the eclipse
point-star pass grows from 0.021 to 0.031 ms at 1080p and from 0.021 to 0.070 ms
at 4K: more catalog points and integrated filtering are not free. The 20% result
is for the complete sky, not that small point-star pass alone. The conservative
solar bound offsets part of the cube-sampling cost, particularly in daylight.
These are shared-desktop GPU timings, not power-consumption measurements or a
guarantee for all graphics drivers. Raw repeated measurements are recorded in
[galaxy-benchmark.json](galaxy-benchmark.json).

A separate 180-frame full-scene 1080p eclipse comparison measured GPU medians
of **4.529 ms baseline / 4.630 ms beta** (+2.2%), with GPU p95 **5.340 / 5.175 ms**.
Synchronized CPU+GPU medians were **5.849 / 6.040 ms**. Both Nix packages built,
all four flake checks passed, and the packaged Hyprlock renderer was exercised
in an isolated headless Hyprland session: native capture, cyan password input
and unlock transition passed. The real desktop was not locked or reconfigured.

```sh
nix develop --command cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
nix develop --command cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
./build/render-tests
./build/ring-render-tests
./build/edge-render-tests
./build/atmosphere-render-tests
./build/scattering-edge-tests
./build/emission-render-tests
./build/sky-render-tests
```

CI runs the same CPU and seven GPU suites using Mesa llvmpipe/Xvfb, builds the
Nix packages and validates the native lock configuration. The feature remains
unmerged for operator review.

## Automated checks

The local CMake/CTest gate covers the 1.25 apocenter/pericenter ratio, closed
Keplerian paths, observer inclination of exactly 10 degrees and all other
orbital planes within 10 degrees of the giant’s equator, orbital clearance including the enlarged moon radii, local
coordinate frames, total/partial/annular eclipses, and invalid CLI arguments.

The actual GLES renderer is exercised on an AMD Lucienne integrated GPU. GPU
regressions check complete mountain occlusion, visible celestial bodies,
daylight/night contrast, deterministic output after resize, and preservation
of host blending state. The mountain occlusion test renders the scene both
with and without celestial bodies: interior mountain pixels must be identical. A second GPU regression injects
extreme radiance into the sky and solar-glare stages at day, sunset, twilight
and night; opaque mountain pixels must remain unchanged. This regression was
also run against the previous composition order and correctly failed.

GitHub Actions additionally builds with Ubuntu system libraries and renders on
Mesa llvmpipe under Xvfb. The Nix gate builds the standalone renderer and patched
Hyprlock, checks the preview launcher, and parses the lock configuration.

## Visual evidence

These are framebuffer captures from the renderer, not generated concept art:

- [Daylight](../assets/preview-day.png)
- [Sun and atmospheric glare](../assets/preview-sun.png)
- [Twilight](../assets/preview-twilight.png)
- [Night](../assets/preview-night.png)
- [Native Hyprlock input display](../assets/preview-lock.png)
- [Observer-moon eclipse shadow on the giant](../assets/preview-eclipse.png)
- [Ring shadows and the unlit ring face](../assets/preview-ring-shadow.png)
- [Giant surface without the former dotted stripe](../assets/preview-surface.png)

Standalone capture times are 0 (day), 270 (Sun), 1180 (twilight), 900 (night),
1000 (ring shadows), and 391780 (observer-moon eclipse during a later season).
The native capture follows the running clock.

[MP4 preview](../assets/preview.mp4) is H.264, 1280x800, 30 fps, 9 seconds.
[Animated WebP](../assets/preview.webp) is 960x600, 20 fps, looping.
Both show a deliberately accelerated day (200x normal screensaver time) for
review. Normal playback takes thirty minutes per solar day.

## Measurement method

`--benchmark` uses an offscreen window at the requested resolution. It reports
both synchronized CPU+GPU wall time and GPU elapsed queries where supported.
The benchmark retains the configured frame cap, so it exercises ordinary
power-management behavior rather than an unlimited busy loop. Results are
rendering time, not a measurement of watts. Startup shader compilation and LUT
construction occur before timed frames.

Measured at 1920x1080 on AMD Lucienne integrated graphics (Mesa radeonsi),
180 frames per scene, capped at 30 fps:

| Scene | GPU median | GPU p95 | CPU+GPU median | CPU+GPU p95 |
| --- | ---: | ---: | ---: | ---: |
| Day | 2.91 ms | 3.52 ms | 4.41 ms | 5.16 ms |
| Night, large illuminated giant | 2.86 ms | 3.74 ms | 4.41 ms | 5.26 ms |

These measurements include the Earth-sized observer moon, enlarged companion
moons, solar glare, slightly
brighter night and fully opaque foreground. They were taken in a live desktop
session, not an exclusive GPU laboratory environment.

## Native integration

The packaged Hyprlock renderer acquired a session lock and rendered correctly
on an isolated Hyprland headless output. Virtual keyboard input also confirmed
the cyan password segments render correctly over the scene. It survived a DPMS
off/on cycle, and
SIGUSR1 exercised the normal unlock transition and clean exit. The native
headless backend continues issuing frame callbacks with DPMS off, so this
test does not establish physical-display power consumption. On outputs whose
compositor suspends callbacks, rendering follows that suspension.

The standalone preview guard was tested against the isolated compositor: it
terminated its child and exited cleanly when the output powered off. Actual
successful PAM password authentication was not exercised.

## Limits

This is an artist-directed Keplerian system, not a stable N-body or tidal
habitability model. Twenty moons are simulated; only geometrically visible
bodies are shown. Moons use procedural surface variation, while the giant uses
a generated high-resolution map. The mountain layer supports changing color
and illumination but not geometrically exact moving mountain shadows.

Scattering uses reduced LUT sample counts and isotropic higher-order feedback.
Local eclipse visibility scales the atmosphere rather than integrating moving
planetary shadow volumes through it. Simultaneously overlapping penumbras use
the darkest single-occluder visibility. Thin clouds trade volumetric depth for
low rendering cost. These are explicit approximations, not path-traced images.

## Narrow ice-ring update

The ring GPU tests construct known geometric alignments and compare the actual
renderer against shadow-disabled reference shaders. They separately verify
planet-to-ring, moon-to-ring, ring-to-planet and ring-to-moon shadows. Three
band densities test a moon in front of and behind the disk: foreground moons
block rings, thin bands/gaps reveal background moons, and dense bands attenuate
them without becoming fully opaque. The existing mountain/glare occlusion and
GL state checks still pass; depth function, depth-write mask and clear depth
are now included in host-state restoration checks.

An additional GPU regression renders a foreground moon and its background ring
with equal radiance. The complete moon silhouette must remain seamless, including
partially covered pixels. Restoring the former depth-tested ring pass in an
isolated test build failed with a maximum 241/255 channel difference; corrected
composition passes with zero difference on this GPU. It adds no framebuffer,
blur pass, or multisample storage.

Matched 1920x1080 measurements on the same AMD Lucienne GPU, 180 frames at scene
time 900 with the final inclined orbits and corrected edge composition, capped
at 30 fps:

| Mode | GPU median | GPU p95 | CPU+GPU median |
| --- | ---: | ---: | ---: |
| Rings disabled (`--no-rings`) | 2.69 ms | 4.01 ms | 3.83 ms |
| Narrow translucent rings and all shadows | 3.20 ms | 3.64 ms | 4.28 ms |

The added median GPU time is about 0.51 ms. The new pass uses one static 4096x1
RG16F radial profile (about 32 KiB including mipmaps), plus a 24-bit scene-depth
attachment (driver storage typically 8 MiB at 1080p). These are timing and memory
measurements/estimates, not watt measurements.

## Silhouette antialiasing

A dedicated GPU test isolates the real mountain coverage and ring annulus,
then compares 320x180 output against 1280x720 output averaged into the same
pixels (16 samples per reference pixel). It checks both coverage error and
retention of partially covered pixels at several ring orientations and nearby
animation times. Corrected mountain edge error is 12.49/255; ring edge error is
about 5–9/255. An isolated build with the previous mountain alpha treatment fails
at 107.60/255; restoring hard ring boundaries separately fails at 57.94/255.
These failures confirm the tests catch both original problems.

The mountain interior still passes the extreme sky/glare occlusion test. The
solution uses upload-time alpha preparation, ordinary texture filtering and
analytic ring coverage; it does not add a full-screen antialiasing pass or a
multisampled framebuffer.

A longitude-chart regression rotates the spherical coordinate chart by half a
turn while compensating the texture coordinate, leaving the physical image
unchanged. At times 550, 650 and 900, the corrected renderer differs by at most
1/255 from the alternate chart. It also reproduces the reported dotted surface
stripe in an isolated build using implicit texture gradients, which fails at
70/255 maximum difference.

## Gas giant atmosphere

The float-framebuffer GPU test exercises the actual atmospheric shader, not a
CPU duplicate. Curved columns differ by at most 0.0191% from independent
20,000-step spherical density integration. Six limb samples differ from a
192-sample shader reference by at most 0.00551 in radiance normalized to unit
solar irradiance across the tested phases and impact parameters. The cached
cloud-disk solution differs from analytical rendering by at most 1/255 in the
final daylight/night images, and zero at the eclipse-contact test time.

Tests also check finite nonnegative radiance, bounded opacity, restrained
molecular blue scattering, an entirely dark rim in a central eclipse, complete
moon-shadow extinction, partial transmission through ring shadows (0.215 in
the fixture), and zero atmosphere beyond its physical cutoff. A supersampled
silhouette reference checks moving atmospheric edges at two nearby times:
mean edge error is 19.8/255, with over 80% of partial pixels retaining coverage.
The test caught and fixed a low-resolution interior/limb boundary that had
prematurely bypassed subpixel integration.

The existing mountain, ring transparency, shadow, resize and longitude-seam
regressions pass. The ring-to-planet fixture now explicitly verifies that its
receiver faces both the observer and Sun; its old nominal point was on the
far hemisphere. Shadow-disabled references now disable both surface and
atmospheric ring attenuation.

[The contact image](../assets/preview-atmosphere-contact.png) is an actual
1920x1080 capture at scene time 376368. Daylight and nighttime captures were
also inspected. No persistent full-circle halo is added in eclipse darkness.

At 1920x1080, 180 frames, 30 fps on the same AMD Lucienne integrated GPU:

| Scene/build | Median GPU time |
| --- | ---: |
| Previous installed renderer, time 391780 | 3.59 ms |
| Atmospheric renderer, time 391780 | 4.71 ms |
| Atmospheric renderer, eclipse contact at 376368 | 4.18 ms |

The measured incremental median is approximately 1.12 ms. Shared-desktop
activity produced large tail-latency spikes in some runs, so these are not
exclusive-device laboratory measurements or watt estimates. The new cache
uses 128 KiB, generated once; limb work is restricted to the narrow edge.


### Atmospheric antialiasing correction

The original four uniformly spaced subpixel rays missed the physical haze
between samples. This produced dotted arcs and up to 44.3% brightness variation
when the image moved by fractions of one pixel. A second bug left cloud
illumination at zero for inner rays handled by the limb shader, creating a
dark seam. The old coverage-only test did not establish correct scattering.

The replacement uses analytic pixel-area coverage, two cloud-region rays and
four density-weighted atmospheric rays. Density-space quadrature always
samples the thin layer without enlarging it. The six path samples are also
normalized to the curved column rather than losing the dense lowest layer.

A new GPU regression measures linear HDR radiance against a reference rendered
at eight times the width and height. It covers three lighting phases and four
quarter-pixel camera offsets. Eclipse-contact relative radiance error is under
1.8%, with 0.49% flux variation; all tested phases remain below 8.5% radiance
error and 1% flux variation. The lit, uniform-cloud edge agrees within 0.20%.
A separate silhouette reference improved from about 19.8/255 error to 0.36/255;
its acceptance threshold is now 2/255 with at least 95% of partial pixels
retaining coverage. Restoring the previous renderer fails the scattered-light
reference at 249% relative error and the temporal stability check.

Updated eclipse-contact and night captures were inspected, along with a short
sequence at 0.1-second simulation steps. At 1920x1080, 180 frames and the same
30 fps cap, measured median GPU time is 5.75 ms at scene time 391780 and 4.63 ms
at eclipse contact (376368). The correction adds no texture allocation, blur,
multisampled framebuffer or fullscreen pass. These remain shared-desktop
timing measurements rather than power measurements.

## Lightning and visible auroras

The new CPU gate integrates a complete flash train at 15, 30 and 60 fps. All
three recover the same 9.33962e10 J over the 120-second deterministic fixture;
short pulses retain their energy without longer artificial lifetimes. It also
checks physical footprint bounds, finite power, corotation and visible auroral
radiance limits.

Actual GPU emission is compared with 2560x1440 reference frames reduced to
320x180. Lightning flux agrees within 1%; quarter-pixel motion variation is
below 1%. Auroral flux differs by less than 5% with under 3% motion variation
in the fixture. These tests exposed half-float/texture-sampling precision loss
and missing endpoint coverage in an initial narrow-ribbon implementation.

Further checks verify that darkness does not turn off electrical emission,
foreground moons block it, background moons do not, the giant hides far-side
lightning, and real foreground ring geometry transmits some light while
attenuating it. The ring fixture transmits 0.739 versus 0.668 at its analytic
center ray; pixel filtering averages neighboring ringlets. The actual opaque
mountain compositor is checked separately. Existing ring, mountain, atmosphere,
longitude-seam and host-GL-state regressions pass.

At 1920x1080/30 fps on the AMD integrated GPU, 120-frame median GPU time was
5.115 ms before and 5.238 ms with emissions: about 0.12 ms additional work in
this sample. CPU+GPU medians were 6.445 and 6.745 ms. These are shared-desktop
measurements, not exclusive GPU measurements or power readings.

Before enabling orbital compression for lightning, a 120-second real-weather
sample at fixed 1080p views found 24 faint flashes
(at least 2/255 display-channel change) during total eclipse at scene 430350,
with four at least 8/255. At evening scene 550, eight exceeded 2/255 and one
exceeded 8/255. At night scene 900 the nearly full giant hid all flashes below
2/255. These thresholds describe rendered pixels, not human detection limits, and the
old rates are not predictions for the final accelerated model.
Actual full-resolution night/eclipse captures and a six-second 30-fps sequence
were generated for review. The aurora is intentionally extremely subtle at
the low-latitude viewpoint. Subsequent final-output checks exposed near-zero
visibility at the original exposure; the dark-scene adaptation described below
addresses lightning without changing its physical energy.

After switching lightning to orbital time, tests also integrate complete event
windows at 15/30/60 fps for 30-second, 30-minute and 24-hour solar days. All
recover the same physical energy for their respective windows, including the
fastest setting where many complete events occur within one display frame.

### Final display visibility regression

The complete 1920x1080 RGBA8 output is compared with emissions disabled. Before
the subsequent 5× visibility boost, at
scene 430350 and accelerated weather 430354.833333333, a lightning flash changes
the peak channel by 49/255 after bounded dark-scene adaptation (previously
5/255). At scene 437625 and weather 103.04, aurora changes the peak channel by
4/255. Tests require at least 20 and 2 respectively, and verify daylight still
washes out the flash. The aurora check proves survival through the compositor,
not naked-eye perceptibility; it remains extremely subtle. All six GPU suites
continue to check ring shadows, silhouettes, terrain, and scattering edges.

### Fivefold emission brightness

With the requested 5× radiance gain, the same final-display samples measure
141/255 for lightning and 11/255 for aurora (formerly 49 and 4). The isolated
lightning reference flux is 0.000142127, versus 0.0000284165 before the gain;
the aurora reference is 0.0000324339 versus 0.00000646088. Half-float rounding
accounts for the small deviation from exactly five. All nine CPU/CLI tests and
six GPU suites pass, including daylight washout and foreground occlusion.

### Independent brightness controls

Seventeen CPU/CLI tests cover the brightness range, fractional values, zero,
nonfinite input and values beyond the limit. The six GPU suites pass. New
isolated HDR checks exercise each effect at 0, 0.5, 5 and 1000 and verify linear
brightness scaling, zero output when disabled and independence from the other
control. Before the wider lavender style below, default final-display samples
remained 141/255 and 11/255.

Before the style change, a real CLI capture at scene 437625 / weather 103.04
with aurora brightness 1000
and lightning brightness 0 shows the reddish arc near the upper-left limb;
compared with both controls at zero, the peak displayed increase is 222/255.
The controls alter emission radiance only and do not add render passes.

### Wide moving lavender auroras

Auroras now use 2800 km FWHM ribbons with pale lavender radiance and a default
brightness multiplier of 1000 (200 times the earlier 5). Continuous traveling
waves deform the ovals and modulate their width; bright patches drift along
them. GPU tests keep the planet fixed and verify spatial change over 30 seconds
and continuity over a millisecond. Wider filtered ribbons are clipped to the
120 km emitting shell, preventing stray spikes beyond the atmospheric limb.
The supersampling, occlusion, independent gain and final-display checks still
apply. Reviewed 1080p captures across the eclipse transition.
