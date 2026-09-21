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
4. Full-resolution sky, sparse star points, and one analytic ring-disk draw.
5. Scissored sphere draws sorted by depth, with foreground rings composited before edge coverage.
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
texture coordinates use the same 26.7-degree tilted pole. Every moon orbit is defined in the giant’s equatorial coordinate system.
The observer orbit is inclined exactly 10 degrees; other orbital planes span
2–10 degrees with varied ascending nodes. Outer irregular moons retain their
retrograde direction. The rings use the giant’s pole directly; no independent
ring tilt exists. The observer’s synchronous rotation and local ground frame
follow its inclined orbital plane. This naturally takes the moon above and
below the rings, with two edge-on crossings per orbit.

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

The translucent disk is drawn first. Each sphere analytically composites the
portion of the disk in front of its surface before applying silhouette coverage.
This preserves partial-pixel coverage: a sphere edge blends against the ring
behind it rather than letting a full-pixel depth rejection expose bare sky.
Foreground moons occlude rings; background moons show through according to optical
depth. Sphere fragments write analytic distance into a 24-bit depth attachment
for sphere-to-sphere ordering. No multisampling, blur, or extra framebuffer is needed.
Both disk and sphere radiance receive the same foreground atmosphere.
The mountain silhouette is composed last and remains opaque. Its source alpha
is converted to opaque-ridge coverage once at texture upload, with premultiplied
color and mipmaps. Sampling preserves coverage without another alpha threshold;
up to 4x anisotropic filtering keeps the vertically compressed terrain detailed
where supported. The ring annulus uses pixel coverage of its projected inner
and outer conics. Derivatives are evaluated before divergent hit rejection so
boundary mip levels remain stable. No full-scene supersampling is used. The giant’s longitude gradients are
wrapped before explicit-gradient texture sampling, preventing the atan branch
cut from choosing an excessively coarse mip level and drawing a dotted stripe.

Planet and moon shadows on the rings use the same finite solar-disk sphere
occlusion routine as body-to-body shadows. Ring shadows on spheres intersect
the same disk and sample the same optical-depth profile. Three radial samples
approximate the finite Sun's penumbra; mip filtering suppresses crawling fine
shadows. Local ring eclipses also affect the observer's Sun and sky. No shadow
maps or additional scattering passes are required. Exactly coplanar rays see
no thickness: vertical ring structure and self-gravity wakes are not simulated.


## Gas giant atmosphere

A separate warm H2/He layer replaces the former decorative blue rim. The model
uses T=280 K, molecular mass 2.3 u and g=74.36 m/s² (three Jupiter masses at one
Jupiter radius): H=kT/(mg)=13.62 km, or 0.0001905 renderer length units. Density
falls exponentially and truncates at 12 scale heights; this is less than 0.23%
of the giant's radius. These are plausible fictional atmosphere assumptions.
Being at 1 AU constrains irradiation but does not uniquely fix temperature,
cloud pressure, composition or aerosol abundance.

RGB molecular vertical optical depths are 0.0011/0.0026/0.0065 at representative
680/550/440 nm wavelengths, approximately a 0.1-bar H2/He column under this high
gravity. They follow the H2 wavelength dependence described by the
[NASA PDS atmospheres node](https://pds-atmospheres.nmsu.edu/education_and_outreach/encyclopedia/rayleigh_optical.htm).
A weak neutral aerosol has vertical optical depth 0.00025, single-scattering
albedo 0.98 and Henyey–Greenstein g=0.65. Its abundance/particle behavior are
assumptions, not claims about a measured extrasolar atmosphere. Rayleigh and
normalized aerosol phase functions set color/intensity relative to the existing
unit solar irradiance. There are no emissive terms or eclipse-only multipliers.

Curved exponential columns use a Chapman approximation with a stable erfcx
expression. A 128x128 RGBA16F table (128 KiB) caches the illuminated cloud-disk
single-scattering solution and spectral extinction. It is generated once;
quadratic angular coordinates resolve grazing sunlight. The giant's interior
and limb use separate shader variants so expensive limb work does not penalize
all disk pixels. Near the limb, six bounded path samples integrate density,
Beer–Lambert extinction and light. An analytic square-pixel coverage function integrates the cloud silhouette.
The outside atmosphere is integrated in exponential-density coordinates, so
samples cannot miss its subpixel thickness. Cloud and atmospheric portions of
the footprint are integrated separately; inner edge rays retain their cloud
illumination. The path quadrature is normalized to the curved column so it
does not lose the dense lowest layer as resolution changes. No full-screen
blur, extra framebuffer, shadow map or MSAA allocation is added.

The finite solar disk is clipped by the cloud horizon. A partially exposed
Sun uses an exposed-cap direction for its slant optical depth. The giant's
shadow can therefore extinguish the complete visible rim during a deep eclipse
from this very close moon. Moon and ring shadows are evaluated along the limb
path; the disk interior reuses its cloud shadow. Ring shadow sampling uses the
finite solar footprint, avoiding derivatives inside divergent ray loops. The
sphere/ring composition and opaque foreground ridge remain shared with the
existing renderer.

Limits: single scattering, an isothermal atmosphere and prescribed haze; no
refraction, spectral absorption bands, cloud/atmosphere multiple scattering or
latitude-dependent haze. The disk solution approximates a thin curved layer by
its integrated columns. Off-disk extinction uses mean RGB transmission because
standard premultiplied-alpha blending has one opacity; scattering and on-disk
extinction are RGB. This avoids another scene copy/composition pass for the
subpixel rim. The existing moon-sky eclipse approximation remains unchanged.
