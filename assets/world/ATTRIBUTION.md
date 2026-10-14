# World terrain credits

The fictional continental field, climate field, stochastic material cache and
dense crown-normal/occlusion field and flight envelope are original outputs of the MIT-licensed producer
scripts in this repository. No photographic landscape or vegetation input is
used.

`relief.ggrelief` derives from numerical elevation credited to **Federal Office
of Topography swisstopo**, with Mapzen Skadi as the distant/edge fallback.
Published swissALTI3D tiles, immutable source URLs and SHA256 checksums are
recorded in [the terrain source manifest](../terrain/sources.json).

`relief-material.png` contains normals, landform curvature and terrain skylight
occlusion computed from that same elevation. The producer uses an original
implementation of Lionel Moisan's [periodic-plus-smooth decomposition](https://doi.org/10.1007/s10851-010-0227-1)
to reconcile tile boundaries while preserving interior ridges and valleys.

Retain the underlying source attribution and terms described in
[terrain attribution](../terrain/ATTRIBUTION.md); the MIT code license does not
replace the source geodata terms. The world height/relief hashes and parameters
are recorded in [manifest.json](manifest.json).

## Biome relief library

The fictional moon also uses modified numerical relief from Mapzen/Tilezen
Skadi tiles N36W113 (Colorado Plateau canyon drainage) and N48W122 (Pacific
Northwest foothills). United States 3DEP, SRTM and GMTED2010 elevation data are
courtesy of the U.S. Geological Survey; terrain tile service: Mapzen. These
US-government source data are public domain. The renderer's MIT license does
not imply authorship or endorsement of USGS data.

Source URLs and checksums, sampled windows, height scaling and derived file
checksums are recorded in [landforms/manifest.json](landforms/manifest.json).
Modifications: local extraction, mean removal, periodic-plus-smooth boundary
correction, filtering and relative-height scaling for a fictional world.
No photographic colors or geographic claim of an Earth location are used.
Provider attribution guidance: [Tilezen data attribution](https://github.com/tilezen/joerd/blob/master/docs/attribution.md).
