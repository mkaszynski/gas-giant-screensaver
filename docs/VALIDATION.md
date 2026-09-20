# Validation

## Automated checks

The local CMake/CTest gate covers the 1.25 apocenter/pericenter ratio, closed
Keplerian paths, orbital clearance including the enlarged moon radii, local
coordinate frames, total/partial/annular eclipses, and invalid CLI arguments.

The actual GLES renderer is exercised on an AMD Lucienne integrated GPU. GPU
regressions check complete mountain occlusion, visible celestial bodies,
daylight/night contrast, deterministic output after resize, and preservation
of host blending state. The mountain occlusion test renders the scene both
with and without celestial bodies: interior mountain pixels must be identical.

GitHub Actions additionally builds with Ubuntu system libraries and renders on
Mesa llvmpipe under Xvfb. The Nix gate builds the standalone renderer and patched
Hyprlock, checks the preview launcher, and parses the lock configuration.

## Visual evidence

These are framebuffer captures from the renderer, not generated concept art:

- [Daylight](../assets/preview-day.png)
- [Sun and atmospheric glare](../assets/preview-sun.png)
- [Twilight](../assets/preview-twilight.png)
- [Night](../assets/preview-night.png)
- [Observer-moon eclipse shadow on the giant](../assets/preview-eclipse.png)

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
