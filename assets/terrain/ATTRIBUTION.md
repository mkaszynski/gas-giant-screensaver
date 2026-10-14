# Terrain sources

The landscape is rendered from numerical elevation, not photographic color.

Nearby relief: **Federal Office of Topography swisstopo**, swissALTI3D, published
2 m LV95/LN02 tiles near Oeschinensee. The exact tile URLs and SHA256 checksums
are in [sources.json](sources.json). Acquired 2026-10-12 through the official
STAC download API. Use, modification, redistribution and commercial use are
permitted with the source reference above under the
[swisstopo OGD terms](https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices).
These are published production geodata, not the restricted sample dataset.

Distant relief: Mapzen/Terrain Tiles Skadi N46E007, a numerical bare-earth
height field. Source URL and checksum are in [manifest.json](manifest.json).
**Mapzen; Europe terrain data produced using Copernicus data and information
funded by the European Union — EU-DEM layers; global GMTED2010 and SRTM terrain
data courtesy of the U.S. Geological Survey.** See
[Terrain Tiles source attribution](https://github.com/tilezen/joerd/blob/master/docs/attribution.md).

The height field is modified: vertical relief is scaled to 0.5 for composition;
modeled conifer crowns, fine relief, an idealized lake mask and a low observer
outcrop are added. Surface albedo, normals and horizon shadows are generated
numerically. This is an Earth terrain analogue on the fictional moon, not a
survey-accurate or navigational reconstruction of the Swiss site. The original
procedural code is MIT; source-data attribution and terms remain applicable to
the derived terrain cache.
