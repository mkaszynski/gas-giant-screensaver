# Sky asset attribution

`stars.bin` is an adaptation of **HYG Database v4.1**, by David Nash / Astronexus,
which combines Hipparcos, Yale Bright Star and Gliese catalog data:
https://github.com/astronexus/HYG-Database .

The catalog and this binary adaptation are licensed under **Creative Commons
Attribution-ShareAlike 4.0 International**. The full license is included in
[LICENSE-CC-BY-SA-4.0.txt](LICENSE-CC-BY-SA-4.0.txt).
Changes: magnitude <= 6.5 selection, omission of the Sun, J2000 equatorial to
ecliptic conversion, magnitude-to-relative-flux conversion, B-V-derived
blackbody colors, and a compact binary representation. No endorsement implied.

`galaxy-*.ggsky` contain our deterministic stellar-population radiance model,
with extinction derived from these numerical measurements:

- **CSFD: The Corrected SFD Milky Way Dust Map**, Yi-Kuan Chiang (2023),
  https://doi.org/10.5281/zenodo.8207175 . The pinned `csfd_ebv.fits` supplies
  the detailed all-sky dust column, in Galactic coordinates. It removes the
  reconstructed cosmic infrared background contamination from SFD98.
- **A Parsec-Scale Galactic 3D Dust Map out to 1.25 kpc from the Sun**,
  Gordian Edenhofer, Catherine Zucker, Philipp Frank, Andrew K. Saydjari,
  Joshua S. Speagle, Douglas Finkbeiner and Torsten Enßlin (2023/2024),
  https://doi.org/10.5281/zenodo.8187943 . The pinned v1.0 MEAN radial-density
  data and inner-column/distance metadata supply the local depth distribution.
  The unused uncertainty HDU is not downloaded. Cite the accompanying paper:
  https://doi.org/10.1051/0004-6361/202347628 .

Both dust datasets are **Creative Commons Attribution 4.0 International**.
The model cube is distributed under that license; the full text is included in
[LICENSE-CC-BY-4.0.txt](LICENSE-CC-BY-4.0.txt). Changes: integrate measured mean
radial densities into cumulative extinction, interpolate the angular fields,
reconcile local and total columns, model unmeasured distant dust, perform
spectral stellar-emissivity integration, and encode linear HDR radiance with
mipmaps and offline BC6H compression. No endorsement implied.

The model does not sample NASA's star-map raster or any RGB sky photograph.
The intrinsic disk/bar luminosity distributions and population spectra are
approximations; measured dust constrains their extinction, not every unseen star.
Details and limitations are in `docs/GALAXY.md`.

The repository's MIT license applies to original code rather than replacing
these separate data licenses. Catalog parameters are in `catalog-manifest.json`;
model parameters/checksums are in `model-manifest.json`. Inputs are fetched and
verified only by offline development tools. The running screensaver contacts
no external service.

The portable BC6H decoder is **bcdec v0.985**, copyright 2022 Sergii Kudlai,
used under its MIT license: https://github.com/iOrange/bcdec . The original
header is retained in `src/third_party/bcdec.h` (SHA-256
`134520764d96f70a27db814173616c89ad85db95fbd3328417c7a8c77e7ca189`).
Its copyright and [MIT license](LICENSE-BCDEC-MIT.txt) ship with this package.
