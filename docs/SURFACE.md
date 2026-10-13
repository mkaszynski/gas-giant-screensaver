# Three-day flight over the moon

The default preview and native Hyprlock widget fly over an Earth-size moon.
The closed route is a constant-speed small circle 59.5 degrees from the mean
subplanet point, entirely on the Jupiter-facing hemisphere. It crosses
59.5 degrees north and south, forests, grasslands, drylands, tundra, snowy
uplands, coasts, open water and sea ice. One circuit takes **259,200 real seconds
(72 hours)**. Its approximately 34,500 km length gives about 133 m/s ground speed.

This is an airplane-height flight. There are **no sideways terrain detours**,
and the camera does not follow every ground bump. A cached altitude envelope
anticipates hills, stays at least 600 metres above the terrain, and limits
normal climb/descent to 3.5 m/s. Over valleys it can be higher. A periodic cubic
B-spline makes altitude, velocity and acceleration continuous at the loop.

The flight clock is independent of the accelerated astronomical clock. Default
startup uses Unix time; opening another preview resumes the world's journey.
Only the route phase wraps, never the astronomical year. The camera faces the
mean subplanet meridian rather than chasing accelerated orbital libration.
Jupiter moves naturally within the frame and remains visible through the route.
Large rings can extend outside the frame. Its physical size and orientation
are preserved.

## Camera, terrain and lighting

The same geographic position and flight altitude feed terrain, celestial
parallax, eclipses, atmospheric scattering and clouds. Geographic north is
corrected at the boundary to the legacy astronomical frame; east follows the
moon's prograde rotation. Viewing elevation is 19 degrees with the existing
58-degree vertical field of view. There is no walking animation or camera roll.

A cached 2048x1024 continental field supplies heights, moisture and mountain
uplift. A 2048x2048 relief cache preserves a connected 32 km region of measured
Swiss/Skadi ridges and valleys at 16 m spacing. A periodic-plus-smooth boundary
correction removes tile-edge mismatches while retaining interior landforms;
mild coordinate warping avoids repeatedly stacking incompatible mountain shapes.
A second cached field supplies residual surface normals, valley/crest curvature
and eight-direction terrain skylight occlusion. Filtered texture gradients keep
this fine relief stable in the distance. Climate uses latitude, altitude and moisture. It is an
idealized annual Earth-like climate, not a coupled weather or hydrology model.
See [world credits](../assets/world/ATTRIBUTION.md) for the numerical elevation
sources. No photographic landscape or vegetation inputs are used.
The relief lighting cache occupies about **21.3 MiB** of GPU memory including
mipmaps and adds one filtered lookup per land pixel; it is skipped on pure water.

Forests form a continuous canopy volume, rather than widely separated dots.
A cached 1024x1024 field stores densely packed, overlapping crown normals,
heights and ambient occlusion. Hardware mips filter crowns at distance. Broad
material variation breaks up color repetition. Individual close tree cutouts and their draw/shadow work have been removed: at
this height those nearby trees cannot enter the frame. The canopy stays dense
and relightable without spending resources on invisible branch details.

Direct light uses atmospheric solar transmission, terrain shadows and eclipses.
Soft skylight and directional, nearly white planetshine light shaded surfaces;
planetshine uses the same irradiance-to-radiance scale as direct sunlight. Terrain cloud shadows
sample the same spherical cloud sheet and wind offsets as the visible clouds.
Aerial transmission uses the atmosphere's Rayleigh/Mie coefficients and height
densities. Forest crown orientation and occlusion affect lighting instead of
being painted highlights.

## Rendering bounds

A spherical radial mesh holds **557,600 vertices and 3,342,336 indices**, covering
145 km. Of 544 angular samples, 512 cover the forward 120-degree sector and 32
cover the back sector for shadows. 1,024 radial rings concentrate resolution
near the camera. Double precision precedes conversion to local GPU coordinates.
Area-weighted normals come from actual triangles.

One background worker replaces geometry after 750 m of flight. Normal frames
retain the old mesh until the replacement is ready. Initial loading and
explicit jumps over 2 km build synchronously. Vertex/instance buffers retain
capacity; unchanged triangle topology uploads once. There is no runtime route
search, horizon scan, network access or GIS dependency.

Two cached 2048-pixel shadow cascades cover 8 km and 60 km extents. Each holds
a current and an astronomically predicted future depth map. Shadow visibility
blends smoothly between those states, then reuses the future map as current
without changing its depth pixels. The hidden old map is recycled for the next
prediction. Near/far updates are staggered: normally only one depth map updates
on a given frame, at 1/2-second intervals under a high sun and 0.5/1-second
intervals near sunrise/sunset with the default 30-minute day. Cadence scales
with the configured day length. Large debug seeks rebuild both pairs once.
Direct sunlight, cloud shadows and eclipse visibility still use current time.
Receiver-plane bias and raster slope offset suppress self-shadow stripes.
All four shadow projections retain their physical world positions across mesh
uploads. A stable distance-based cascade transition avoids projection rollover
seams. Most land pixels use only one cascade, retaining eight PCF taps despite
the temporal blend; the narrow cascade transition samples both. The two extra
depth maps add about **32 MiB** of GPU storage (typical 4-byte depth allocation).
Entirely open-water meshes skip these shadow updates, and pure water pixels
skip land material and lighting work.

Up to 4x multisample HDR coverage is resolved before exposure/tonemapping.
Cached conservative bounds for 4,096 terrain blocks replace per-frame projection
of all 557,600 vertices. This optimization preserved the checked forest image
pixel for pixel. A conservative screen bound crops storage and resolve to terrain pixels, with
quantized capacity to avoid reallocating for individual rows. Opaque terrain
occludes celestial objects and solar glare; only silhouettes
blend coverage. The old 88 MiB angular landscape cache is not loaded in flight.

Ocean shading includes Fresnel sky/celestial/cloud reflections, absorbing water
color, and filtered wind-wave normals. Reflections sample the same cloud sheet
from the actual water position; their base normal follows the spherical ocean
instead of adjacent ground slopes. **Lakes/rivers, shoreline surf, coastal land/tree
reflections and coupled seasonal climate are not implemented in this version.**
The procedural world is fictional; it is not a surveyed Earth continent. This
is an improved airborne biome beta, not a claim that every biome is finished
at photographic quality. Distant rock silhouettes and water are areas for
further visual review.

## Preview and reproduce

```sh
nix build .#gas-giant-screensaver -o result-surface
./result-surface/bin/gas-giant-screensaver \
  --data-dir ./result-surface/share/gas-giant-screensaver \
  --fullscreen --time 900 --travel-time 162000
```

`--travel-time` selects a route position without changing sky time.
`--travel-speed` changes the rate for review; default speed 1 completes the
three-day flight. Previewing does not lock the desktop. Escape or Q closes it.
The old fixed alpine renderer is retained only for regression comparisons.

Add `--debug` to display a preview-only time slider. Select **DAY**, **3 DAYS**
or **YEAR** to choose the range, then drag and release to seek. The year range
uses the actual accelerated stellar-year duration, not a modulo of the day.
Switching ranges starts a new window at the current instant. **PAUSE/PLAY** or
Space stops/resumes time; **RESET** or Home returns to the window's start;
Left/Right step by one-thousandth of the selected range. Releasing a drag seeks
once, avoiding repeated mesh rebuilds while moving the pointer.
The compact panel sits in the upper-left corner, leaving the landscape visible.

The slider advances the same absolute sky time used by the route, clouds and
weather, preserving any `--travel-time` or `--weather-time` starting offset.
`--travel-speed` still multiplies route travel. Epoch time is retained; only
each periodic subsystem reduces its own phase. Debug controls are excluded
from captures, recordings, benchmarks and the native lock widget. Ordinary
preview/lock rendering does not initialize or draw this overlay.

Offline regeneration uses pinned elevation downloads and Python requirements:

```sh
python scripts/bake_world.py --source /tmp/N46E007.hgt.gz \
  --swiss /tmp/terrain-swiss --output assets/world
nix develop --command cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_BUILD_FLIGHT_BAKER=ON
nix develop --command cmake --build build --target flight-baker --parallel 3
python scripts/bake_flight.py --executable ./build/flight-baker --output assets/world
python tests/test_world_bake.py
```

The offline flight builder samples **2,097,152 terrain positions**, computes
an upper envelope with bounded slope, filters it, and validates interpolated
clearance, climb rate and Jupiter visibility before replacing the asset. The
32,768-point `GGFLY001` float-height cache is just **128 KiB**. Its periodic
B-spline lookup takes four cached values per frame. The manifest binds it to
both elevation caches with SHA-256. Rebuilding terrain requires rebuilding
this profile.

## Review captures

These are actual 1920x1080 framebuffer captures:

| Surface | Sky time | Route time | Capture |
|---|---:|---:|---|
| Ocean | 1740 | 0 | [Ocean](images/surface/ocean.png) |
| Drylands | 1710 | 16200 | [Drylands](images/surface/drylands.png) |
| Forest | 510 | 162000 | [Forest](images/surface/forest.png) |
| Forest, low sun | 900 | 162000 | [Low sun](images/surface/golden.png) |
| Snowy uplands | 360 | 210600 | [Snow](images/surface/snow.png) |
| Forest twilight | 960 | 162000 | [Twilight](images/surface/twilight.png) |

See [validation](VALIDATION.md) for automated checks and measured performance.
