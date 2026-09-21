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
| Rings disabled (`--no-rings`) | 2.84 ms | 3.39 ms | 4.43 ms |
| Narrow translucent rings and all shadows | 3.26 ms | 6.00 ms | 4.98 ms |

The added median GPU time is about 0.42 ms. The new pass uses one static 4096x1
RG16F radial profile (about 32 KiB including mipmaps), plus a 24-bit scene-depth
attachment (driver storage typically 8 MiB at 1080p). These are timing and memory
measurements/estimates, not watt measurements.
