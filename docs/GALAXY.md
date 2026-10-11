# Earth sky beta

The `feat/galaxy` beta replaces uniformly random stars with an Earth sky catalog
and adds the Milky Way. It does not change orbital simulation, the camera,
clouds, mountains, ring geometry or the operator's configured emission gains.
The feature is available for review without replacing the installed lock screen.

## Astronomical content

- **8,920 stars**, apparent V magnitude <= 6.5, from
  [HYG v4.1](https://github.com/astronexus/HYG-Database). The Sun is excluded.
  J2000 right ascension and declination are converted offline into the same
  ecliptic world frame as the orbit simulation, using 23.4392911-degree obliquity.
  The moon's changing local frame rotates both stars and galaxy together.
- Relative visual flux follows `10^(-0.4 * magnitude)`. B-V photometry produces
  a Ballesteros effective temperature and a blackbody spectrum, integrated
  against [Wyman, Sloan and Shirley's CIE 1931 approximation](https://jcgt.org/published/0002/02/01/).
  Linear RGB is normalized to unit visual luminance; colors cannot accidentally
  alter catalog V brightness. Forty missing color indices use neutral white.
- Diffuse light is derived from the **Milky Way-only** layer of
  [NASA Deep Star Maps 2020](https://svs.gsfc.nasa.gov/4851/), which omits the
  bright Hipparcos/Tycho layer. Bright catalog stars are consequently not drawn
  twice. Dust lanes, star clouds, background clusters and the Magellanic Clouds
  follow the map; this is not a painted procedural stripe.

Positions and magnitudes are a static historical catalog. Proper motion,
precession, variable stars and current transient phenomena are not simulated.
Close binary components can overlap naturally at this field of view. The gap
between the naked-eye catalog limit and the map's omitted Hipparcos/Tycho stars
is not represented as individually resolved points. No deep-sky nebula imagery
or telescope-only detail is added.

## Dark-sky appearance

Stars are compact points with a pixel-integrated Gaussian profile, rather than
uniformly sized disks. The integral preserves flux while stars move through
fractional pixel positions; resolution scaling preserves apparent brightness.
Bright stars show restrained warm or blue color; the diffuse galaxy retains
25% of the source's chroma to approximate its mostly neutral low-light appearance.
The map is averaged in linear light over roughly 0.1 degree to avoid turning
unresolved Gaia stars into noisy bright texels.

The existing Earthlike atmospheric transmittance attenuates both layers,
particularly near the horizon. Atmospheric sky brightness washes them out in
daylight and twilight. Existing clouds, solid bodies, ring transmission and the
opaque foreground ridge compose in front of the sky. The sky itself does not
cast shadows on mountains or change the simulation.

The Milky Way is deliberately subdued relative to a long-exposure astrophotograph.
Its largest features emerge in a dark sky; it is not a saturated colored cloud.
The enormous sunlit giant supplies much more planetshine than Earth's Moon.
A bounded phase/solid-angle contrast factor suppresses the celestial background
beside it. This is **display calibration**, not absolute retinal photometry:
catalog brightness ratios, orientations and extinction have physical meaning,
but monitor brightness, observer dark adaptation and glare are not calibrated
to a particular eye or display. Instantaneous adaptation and the existing
approximate airglow remain limitations. Eclipse time `437625` is useful for
review because the giant is then a dark silhouette.

## Reference comparison

The requested [PyVista 16k space cube](https://docs.pyvista.org/api/examples/_autosummary/pyvista.examples.downloads.download_cubemap_space_16k.html)
was downloaded and its six faces inspected. Its
[provenance](https://github.com/pyvista/data/blob/master/Data/cubemap_space/README.md)
identifies the same NASA map family: the full 16k galactic-coordinate star map.
Its EXR-to-JPEG conversion scales/clips values and uses JPEG quality 50, so it
provides a **morphology reference**, not an absolute naked-eye brightness target.
The beta retains the broad Sagittarius star clouds and branching dark lanes,
without borrowing that reference's exposure or drawing its bright stars twice.
The beta uses the 4k diffuse map and a 1024-pixel cube to keep memory and bandwidth
small. Fine telescope-scale structure is intentionally below the display's
angular resolution; catalog stars retain independent, sharp positions.

## Runtime and performance

The runtime reads one 279 KiB binary catalog and six PNG faces once, then uploads
one static star VBO and one mipmapped sRGB cubemap. It generates neither positions
nor colors each frame. The additional diffuse sky costs one cube lookup in the
existing background pass; there is no extra fullscreen pass, render target,
blur pyramid or procedural galaxy generation. Total extra static GPU storage
is approximately **32 MiB** for the cube/mips plus the catalog VBO.

To accommodate that lookup, the background shader rejects pixels outside the
solar disk with a conservative cosine bound before evaluating `acos`. A GPU
regression compares it against the original ungated disk and limb-darkening
calculation. The existing solar appearance and edge filtering are preserved.

The optional benchmark records background and point-star passes separately,
plus their sum. **The 20% budget is evaluated on combined sky rendering** because
the added galaxy is rendered in the background pass, not the point-star pass.
This scope is explicit: the point-star pass alone does grow with the larger
catalog and integrated filtering. Full-scene rendering is measured separately.
Measurements use the actual AMD integrated GPU on a shared desktop, not an
exclusive GPU laboratory. Timing instrumentation is excluded from production.

For timings and checks, see [validation](VALIDATION.md#earth-sky-beta).

```sh
nix develop --command cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_BUILD_SKY_BENCHMARK=ON
nix develop --command cmake --build build --parallel 3
./build/sky-benchmark 437625 1920 1080 180
```

## Reproduce the assets

Use Python with `numpy`, `scipy`, `pillow`, and `OpenEXR`. These are offline build
tools; the installed screensaver does not depend on Python or those libraries.
Download the following inputs and verify SHA-256:

| Source | SHA-256 |
|---|---|
| [HYG v4.1 CSV](https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv) | `d9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd` |
| [NASA celestial Milky Way 4k EXR](https://svs.gsfc.nasa.gov/vis/a000000/a004800/a004851/milkyway_2020_4k.exr) | `2eb802d6e68d170b410f766c7fec07f7518619f6b6708fdc81e9302d93e74fdb` |

```sh
python scripts/bake_sky.py --catalog hygdata_v41.csv \
  --milky-way milkyway_2020_4k.exr --output assets/sky
```

The bake refuses input whose checksum differs. RA zero is at the NASA panorama's
center; RA increases to the left. Standard OpenGL cube-face directions are
transformed back to equatorial coordinates before sampling that panorama.
Polar rows clamp and longitude wraps. Smoothing and bilinear reprojection occur
in linear light; PNGs encode sRGB, which the GPU decodes before lighting and
filtering. Runtime cube dimensions and catalog values are validated.

The binary layout is `GGSSTAR1`, a little-endian uint32 count, then eight
little-endian floats per star: unit xyz direction, magnitude-zero-relative flux,
unit-luminance linear RGB, V magnitude. `manifest.json` records the selection
and source checksums. The cube is smoothly filtered across face boundaries.

HYG data and `stars.bin` remain **CC BY-SA 4.0**. NASA map attribution, the full
catalog license and the transformations are included with the installed assets
in [assets/sky/ATTRIBUTION.md](../assets/sky/ATTRIBUTION.md). Original code remains
MIT. Credits: David Nash / Astronexus; NASA/Goddard Space Flight Center Scientific
Visualization Studio, Ernie Wright; Gaia DR2: ESA/Gaia/DPAC.
