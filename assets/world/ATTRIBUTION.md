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
