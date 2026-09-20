# Design and verification contract

Goal: a low-power fixed observation of a physically lit fictional gas giant
system, with a Nix preview and native Hyprlock integration.

Scope: one renderer, 20 Keplerian moons, fixed ground observer, atmosphere,
thin clouds, mountain foreground, stars, captures and profiling. No changes to
host idle timers, PAM configuration or the existing installed screensaver.

The giant has Jupiter's radius and three Jupiter masses. Its star has solar
properties at approximately 1 AU. Length calculations use Jupiter radii;
orbit solving and coordinate transforms use double precision. GPU coordinates
are observer-relative. The solar disk radius is 0.00465047 radians.

The home moon has three Earth radii, a semimajor axis of three giant radii, and
eccentricity 1/9: apocenter/pericenter = (1+e)/(1-e) = 1.25 exactly. Synchronous
rotation follows mean anomaly, allowing optical libration. Solar-day duration
is 1800 displayed seconds; the sidereal period includes the annual correction.
Latitude is 38 degrees north, longitude 50 degrees from the mean subplanet point.
The camera is fixed; orbital libration is intentionally visible.

Other orbits have nonzero eccentricity. Inner and major moons are prograde;
the small outer irregular satellites are retrograde. The model prescribes
Keplerian orbits rather than performing an N-body integration. Non-crossing
radial envelopes and bounded geometry are tested; long-term dynamical stability
and tidal habitability are not claimed.

Rendering passes:

1. Transmittance LUT, 256x128, generated once.
2. Isotropic multiple-scattering feedback LUT, 32x32, generated once.
3. Sky-view LUT, 256x128, updated four times per displayed second at normal speed.
4. Full-resolution sky, sparse star points, scissored sphere draws sorted by depth.
5. Thin clouds, alpha mountain layer, exposure and dithering.

One renderer is used by both the GLFW preview and Hyprlock. There are no
runtime network requests, generated assets, terrain meshes, or per-frame
surface-texture uploads. All asset generation is offline.

Risks: GL driver support, high-DPI output sizes, shader failures, lock widget
lifetime, authenticating during transitions, and rendering while displays are
off. Automated tests cover orbital behavior and CLI failures; actual GPU
captures and timings are recorded separately. The lock integration keeps PAM
and uses an opaque fallback if renderer initialization fails.
