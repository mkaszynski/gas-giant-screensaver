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

The home moon has Earth's 6,371 km radius, a semimajor axis of three giant radii, and
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
5. One analytic ring-disk draw, depth-tested against sphere surfaces.
6. Thin clouds and solar glare, opaque mountain composition, exposure and dithering.

One renderer is used by both the GLFW preview and Hyprlock. There are no
runtime network requests, generated assets, terrain meshes, or per-frame
surface-texture uploads. All asset generation is offline.

Risks: GL driver support, high-DPI output sizes, shader failures, lock widget
lifetime, authenticating during transitions, and rendering while displays are
off. Automated tests cover orbital behavior and CLI failures; actual GPU
captures and timings are recorded separately. The lock integration keeps PAM
and uses an opaque fallback if renderer initialization fails.

## Ice rings

The ring disk extends from 1.235 to 1.780 giant radii. Saturn-like C/B/A bands,
the Cassini division, narrow gaps and an F-like ringlet are compressed into half
the original Saturn-width profile. Optical depths are scaled to 55% of that
initial design for a lighter, more translucent appearance. The disk and giant
texture coordinates use the same 26.7-degree tilted pole. Moon orbits are not
moved to avoid ring crossings.

A 4096-by-1 RG16F texture stores normal optical depth and modest particle-albedo
variation. It is generated once and mipmapped; neither particles nor ring
meshes are simulated. View transmission follows exp(-tau/abs(cos(angle))).
The lit and unlit faces use distinct thin-slab reflection/transmission terms.
A small higher-order brightness term and faint planetshine are approximations.
The white ice appearance is artistic: Saturn's real rings are already mostly
water ice ([NASA](https://science.nasa.gov/mission/cassini/science/rings/)).
The compressed band layout follows the general structure in the
[NASA ring fact sheet](https://nssdc.gsfc.nasa.gov/planetary/factsheet/satringfact.html),
not a reproduction of measured Saturn optical-depth data.

Opaque sphere fragments write analytic distance into a 24-bit depth attachment.
The translucent disk is drawn afterward with depth writes off, so foreground
moons occlude rings and background moons show through according to optical depth.
Both disk and sphere radiance receive the same foreground atmosphere.
The mountain silhouette is composed last and remains opaque.

Planet and moon shadows on the rings use the same finite solar-disk sphere
occlusion routine as body-to-body shadows. Ring shadows on spheres intersect
the same disk and sample the same optical-depth profile. Three radial samples
approximate the finite Sun's penumbra; mip filtering suppresses crawling fine
shadows. Local ring eclipses also affect the observer's Sun and sky. No shadow
maps or additional scattering passes are required. Exactly coplanar rays see
no thickness: vertical ring structure and self-gravity wakes are not simulated.
