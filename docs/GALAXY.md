# Rendered Earth night sky

The `feat/galaxy` branch replaces the old uniform starfield with measured Earth
stars and a deterministic model of integrated Milky Way light. It remains
unmerged for operator review. Orbital simulation, celestial geometry, clouds,
mountains and configured giant emission gains retain their existing behavior.

## Stars and diffuse light

The renderer draws **8,920 HYG v4.1 stars**, V magnitude <= 6.5, using measured
J2000 directions, relative flux `10^(-0.4 V)` and B-V-derived spectral colors.
The Sun is excluded. All coordinates/colors are cached in a static VBO.
The pixel-integrated point profile preserves brightness during subpixel motion.
[HYG source](https://github.com/astronexus/HYG-Database).

The unresolved background is generated from an emissivity model, rather than
sampling a flattened star-map image. `scripts/galaxy_model.py` integrates two
luminosity-weighted population templates through smooth thin/thick disks and a
triaxial stellar bar. The Sun is at radius 8.2 kpc and height 20 pc. Old/young
spectra are 4500 K and 8500 K blackbodies, integrated through the same analytic
CIE approximation used for catalog colors. A CCM89 R_V=3.1 extinction curve is
applied spectrally, before conversion to linear RGB.

A mixture of normal luminosity-weighted absolute-magnitude distributions models
giant/dwarf light. Its unresolved fraction depends on distance and extinction;
light brighter than the catalog's magnitude limit is excluded from the smooth
model. This is a population approximation, not an exact Gaia population-synthesis
fit. It avoids adding a second contribution for the bright-star population.
Observed HYG magnitudes already include interstellar extinction and therefore
receive only the observer atmosphere's attenuation at runtime.

For each direction, deterministic distance quadrature computes:

```
L(direction) = integral(emissivity * unresolved_fraction * dust_transmission ds)
```

The units are V-zero-point-relative flux per steradian. The inverse-square
stellar-flux factor cancels the distance-square volume factor. No random rays,
particle sampling, dithering or hash noise generates the model light.
The diffuse and point layers share a magnitude-zero-point scale at the 1080p
reference projection. Display exposure follows the existing screensaver.
This is not an absolute calibration of a particular eye or monitor.

## Measured dust

The detailed angular dust column comes from
[Chiang's CSFD map](https://doi.org/10.5281/zenodo.8207175), HEALPix nside 2048.
This is numerical extinction data with reconstructed extragalactic contamination
removed; no photographic RGB is used. The local distance distribution comes
from the mean of [Edenhofer et al.'s 3D reconstruction](https://doi.org/10.5281/zenodo.8187943),
HEALPix nside 256, extending from the inner 68.6 pc to 1.248 kpc. All measured
radial density shells are integrated, then 49 cumulative knots are retained.
The 496 exact-zero CSFD conversion-seam pixels near the Galactic plane are
filled from valid neighboring columns; they are missing entries rather than
transparent windows into the central bar. Measured nonzero pixels are retained.
The mean is used rather than random posterior samples.

CSFD columns use approximate A_V = 2.742 E(B-V); the local reconstruction uses
approximate A_V = 2.8 E_ZGR. Local columns are bounded by the measured all-sky
column. Beyond the measured volume, the remaining column is distributed through
an exponential gas disk, with 3.2 kpc radial and 120 pc vertical scales. Thus
nearby light can appear in front of a lane while distant light is reddened and
absorbed. The distant distribution and mismatches between surveys are modeled
approximations, not measured 3D detail across the entire Galaxy. CSFD's Galactic
plane calibration and finite angular resolution also limit accuracy.

## HDR cache and runtime

The offline renderer computes the complete sky in bounded ray batches, with
96 logarithmic distance samples from 1 pc to 30 kpc. It stores six 2048-pixel
cube faces in **linear HDR**, builds mipmaps by averaging radiance, and encodes
BC6H with ISPC's slow profile. No intermediate 8-bit image, baked display
exposure, clamped photographic highlights or Gaussian source blur is involved.
The scene stores radiance with a reversible 4096x scale before its RGBA16F
framebuffer, preserving faint values above the coarse subnormal range. The
post pass reverses that scale before exposure. Sky/transmittance sampling
uses explicit high precision; low-precision sampling created visible bands
in elevated-exposure diagnostics. Neither change adds a framebuffer or pass.

Star positions and background structures rotate together in the existing local
frame. The sky retains atmospheric extinction, daylight washout, planetshine
contrast adaptation, foreground occlusion and ring transmission.

The complete compressed cache/mips use approximately **32 MiB**. Runtime uploads
it once and performs **one cube lookup** in the existing background pass.
No galaxy integration, catalog generation, codec or texture streaming runs
during animation. On GPUs lacking BPTC, the included `bcdec` fallback expands
the same HDR values once to RGB16F. Logical storage is 192 MiB; drivers may pad
RGB to RGBA, making it 256 MiB. `--uncompressed-galaxy` tests that fallback.

For visual validation the offline renderer can also output uncompressed RGB16F
reference faces, using the same binary container. These are not shipped. A
separate data directory allows direct raw-reference versus compressed rendering
through the actual GLES pipeline, including at elevated exposure.

The eight-byte magic is `GGSHDR01` (BC6H) or `GGSRAW01` (RGB16F reference), followed
by little-endian uint32 face dimension and mip count. Each mip has a uint32
byte count and payload. The loader validates dimensions, complete mips, lengths,
trailing data, and finite/nonnegative raw radiance. Cube faces must have matching
sizes and encodings. Raw half floats are little-endian; packaged builds target
x86_64 Linux.

## Reproduction and checks

Install the pinned offline dependencies in `scripts/sky-model-requirements.txt`.
These packages are not runtime dependencies or part of the Nix screensaver.

```sh
python scripts/fetch_galaxy_data.py --output sky-inputs
python scripts/bake_stars.py --catalog hygdata_v41.csv --output assets/sky
python scripts/render_galaxy.py --dust sky-inputs/csfd_ebv.fits \
  --profiles sky-inputs/dust-profiles.npz --output assets/sky \
  --reference sky-reference --face-size 2048
python tests/test_galaxy_model.py
```

The downloader validates the pinned SHA-256 of CSFD and the original Edenhofer
primary/MEAN and metadata subsets. HTTP ranges omit the unused 1.6 GB uncertainty
HDU; the retained bytes are original data, not a substituted or fabricated FITS
file. Every requested range must have the exact expected Content-Range/length.
The full-catalog HYG checksum is also pinned. Inputs are immutable versions.
No build or runtime downloads these source catalogs; the generated cache ships.

Model tests cover orientation, magnitude selection, deterministic output,
quadrature convergence, continuous emissivity, absorption/reddening, cumulative
dust-column conservation, spectral color and HDR mip/reference serialization.
GPU tests check the continuous diffuse field, band/pole contrast, catalog flux,
visible points, cube edges, daylight, mountain coverage and portable HDR decoding.
Measurements and framebuffer captures are in [validation](VALIDATION.md).

## Remaining limits

The intrinsic population and bar models are approximations. Fine structure is
constrained by measured dust, with no invented fractal detail. The model does
not reconstruct every observed stellar association, globular cluster or the
Magellanic Clouds; their former raster contributions are removed. Proper motion,
precession and variable stars are not simulated. Atmosphere, airglow and immediate
contrast adaptation retain the existing approximations. The existing sky floor
is not a complete GAMBONS implementation. Its
[scientific model](https://arxiv.org/abs/2408.17371) is a reference for the components
and brightness comparisons, not code incorporated into this implementation.

The goal is a clean, detailed rendering with physically organized light and
extinction, not a claim that modeled unseen structure is more accurate than
measurements. The source NASA/PyVista map remains a historical morphology
reference and is no longer a runtime asset or model input.

Both dust datasets and the rendered derivative cube are CC BY 4.0. The HYG
catalog derivative is CC BY-SA 4.0. Original code remains MIT. See
[sky attribution](../assets/sky/ATTRIBUTION.md) for credits and full licenses.
