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

`galaxy-*.bc7` are adapted from **Deep Star Maps 2020**, the Milky Way-only
celestial-coordinate map by Ernie Wright:
https://svs.gsfc.nasa.gov/4851/ .

Credit: **NASA/Goddard Space Flight Center Scientific Visualization Studio.
Gaia DR2: ESA/Gaia/DPAC.**
NASA SVS describes its visualizations as public domain unless otherwise noted:
https://svs.gsfc.nasa.gov/help/#faq .
Changes: per-texel flux converted to surface brightness, restrained chroma for
photographic appearance, conversion of the 16k celestial map to a 4096-pixel
ecliptic-coordinate cubemap and offline BC7 sRGB compression with linear-light
mipmaps. No Gaussian preblur is used.
These are astronomical data visualizations, not generated artwork.

The repository's MIT license applies to its original code, including the bake
script, rather than replacing the catalog's separate data license.
Source checksums and conversion parameters are in `manifest.json` and
`docs/GALAXY.md` in the repository. The sky is built offline; no external
services or catalogs are contacted by the running screensaver.

The portable BC7 decoder is **bcdec v0.985**, copyright 2022 Sergii Kudlai,
used under its MIT license: https://github.com/iOrange/bcdec . The original
header is retained in `src/third_party/bcdec.h` (SHA-256
`134520764d96f70a27db814173616c89ad85db95fbd3328417c7a8c77e7ca189`).
Its copyright and [MIT license](LICENSE-BCDEC-MIT.txt) ship with this package.
