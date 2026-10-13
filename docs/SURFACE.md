# Three-day surface circuit

The default preview and native Hyprlock widget now travel over an Earth-size
moon. The route is a **closed circuit around a nominal 59.5-degree small circle
from the mean subplanet point**, entirely on the Jupiter-facing hemisphere.
Cached detours of up to 5 km follow higher ground to preserve the view of Jupiter
without increasing camera clearance. It crosses approximately
59.5 degrees north and south. One circuit takes **259,200 real seconds (72 hours)**;
the route clock is independent of the accelerated orbital/day/year clock.
Default startup uses Unix time, so opening another preview does not restart the
journey. Only the route angle wraps; astronomical clocks do not reset with it.

The path is about 34,500 km long, giving approximately 133 m/s ground speed.
Jupiter's apparent direction changes with latitude and physical orbital
libration. The camera follows its bearing. Sampled center elevation remains
above 16 degrees in the tested sweep. Terrain can cover the lower limb, and large rings can extend
outside the frame; the view does not falsify their orientation or size.

## Camera and world

One terrain query sets the camera to `max(ground, water) + 50 m`. The same
altitude and geographic position determine astronomical parallax, eclipses,
atmospheric scattering and cloud projection. Eastward longitude and physical
north are translated into the legacy astronomical frame at one boundary, so
terrain normals and sunlight agree as latitude changes. This replaces the old 2.3 m
terrain-cache camera / 200 m celestial-camera mismatch. Viewing elevation is
19 degrees with the existing 58-degree vertical field of view, showing more
ground from this height. Travel is a flying viewpoint, not a walking character.

The offline builder caches a 2048x1024 spherical continental height field,
moisture and mountain uplift in `moon.ggworld`. It includes ocean floor,
irregular coastlines and mountain belts. Climate classification uses latitude,
altitude and moisture: ocean, sea ice, snowy/tundra uplands, drylands, grassland
and forest all occur on the actual route. Climate is an idealized Earth-like
annual map, not a coupled weather/hydrology or tidally locked climate simulation.

A separate 1024x1024 relief cache contains detrended measured Swiss/Skadi
terrain at 8 m spacing. Smooth edges, two transformed samples and long-scale
coordinate warping prevent hard tile seams. Stable world-coordinate relief
adds detail between the broad cached landforms. Source attribution remains in
[world credits](../assets/world/ATTRIBUTION.md). There is no photographic RGB
landscape input.

## Rendering and bounds

A radial mesh holds 98,688 vertices and 589,824 indices, covering a 145 km
radius. Logarithmic spacing concentrates geometry near the camera. Vertices,
normals and camera transforms use spherical geometry; double precision is used
before converting local coordinates to GPU floats. Area-weighted normals come
from the actual triangles, avoiding four extra terrain samples per vertex.
Water-wave normals retain the spherical sea normal. One background job prepares
replacement geometry after 128 m of movement. Normal frames retain the old
mesh until the new one is ready. Initial loading and explicit jumps over 2 km
build synchronously. Vertex and instance buffers retain capacity across uploads;
unchanged triangle topology uploads once. There is no runtime network or GIS
dependency.

Eight numerically generated conifer/deciduous cutouts are instanced at stable,
jittered surface positions. Heights, tint, moisture, temperature and slope
control placement; distance fading blends into far canopy color. Trees are
billboards, not complete branch meshes. Meter-scale surface texture variation
uses cached stochastic fields with mip filtering. Geological height includes
measured relief; this is not a survey-accurate reproduction of a real continent.

Terrain and vegetation have two cached 2048px depth-shadow cascades (1.8 km and
18 km extents). Normally at most one cascade updates per frame, with 0.5/1 s
intervals. Receiver-plane bias and raster slope offset prevent contour-like
self-shadow aliasing. Shadow transforms retain their moon-fixed position when
new geometry arrives.

Up to 4x multisample geometry coverage is resolved in linear HDR, then
composited before exposure/tonemapping. A conservative screen bound crops the
multisample target to the terrain region. Quantized capacity grows rather than
reallocating when a ridge crosses a pixel row. Opaque surfaces cover celestial
objects and solar glare; coverage blends only edges and foliage gaps. The old
88 MiB angular landscape cache is not loaded in moving mode.

Oceans use Fresnel reflection, absorbing water color, filtered wind-wave normals
and the existing HDR celestial scene. Out-of-frustum reflection uses atmospheric
radiance. **Coastal land/tree reflections, lakes/rivers, shoreline surf and full
seasonal climate are not implemented in this moving-world version.** The old
fixed alpine lake keeps its terrain reflections in comparison mode. This is a
working terrain-travel beta, not a claim of completed photographic-quality
vegetation or a physically complete water renderer.

## Preview and reproduce

```sh
nix build .#gas-giant-screensaver -o result-surface
./result-surface/bin/gas-giant-screensaver \
  --data-dir ./result-surface/share/gas-giant-screensaver \
  --fullscreen --time 1710 --travel-time 162000
```

`--travel-time` selects a point along the route, without changing sky time.
`--travel-speed 0` holds position; a larger multiplier previews travel faster.
Default speed is 1. Historical static captures/eclipse presets use `--fixed-view`.
Previewing does not lock the desktop. Esc or Q closes the window.

Offline regeneration uses the existing pinned elevation downloads and Python
requirements. It never runs in the screensaver:

```sh
python scripts/bake_world.py --source /tmp/N46E007.hgt.gz \
  --swiss /tmp/terrain-swiss --output assets/world
nix develop --command cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DOBSERVATORY_BUILD_CIRCUIT_BAKER=ON
nix develop --command cmake --build build --target circuit-baker --parallel 3
python scripts/bake_circuit.py --executable ./build/circuit-baker --output assets/world
python tests/test_world_bake.py
```

The offline circuit planner samples a corridor, optimizes a continuous closed
path and checks intervening positions against terrain along the mean Jupiter
bearing. Its 28-degree ridge limit retains a visible part of the disk through
orbital libration; it does not force every part of Jupiter above every mountain. It runs
only when rebuilding world assets. Runtime reads the resulting float-offset
cache with periodic cubic interpolation; no search or horizon scan runs per frame.
Regenerating terrain also requires regenerating the circuit. Its manifest binds
the route to both elevation caches by SHA-256.

Assets are checked into the package. See [validation](VALIDATION.md) for the
GPU, route continuity, clearance and performance checks.

## Review captures and visual follow-up

These are 1920x1080 captures from the actual Nix package:

| Surface | Sky time | Route time | Capture |
|---|---:|---:|---|
| Ocean | 450 | 0 | [Ocean](images/surface/ocean.png) |
| Drylands | 480 | 16200 | [Drylands](images/surface/drylands.png) |
| Forest | 1710 | 162000 | [Forest](images/surface/forest.png) |
| Snowy uplands | 30 | 210600 | [Snow](images/surface/snow.png) |

The travel and occlusion behavior is verified, but the moving biome art is
still visibly a beta. The forest has fairly regular spacing and cutout trees;
snowy slopes expose the radial mesh's coarse distant triangles. More natural
vegetation clustering and an adaptive mesh that spends geometry on visible
ridges are the main visual follow-up. A larger uniform mesh would increase
CPU generation and shadow work across terrain outside the view. Ocean surf,
coastal reflections and hydrology remain the separate limits described above.
