# Validation

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
