# Relightable terrain and lake

The former generated mountain panorama was a single color/alpha layer. Its
internal shadows could not move with the sun. The replacement is a fixed-view
geometry cache baked from numerical Earth elevation, with modeled materials,
conifer crowns, fine relief and water. It contains no photographic RGB input.

Nearby mountain relief uses published swissALTI3D 2 m tiles; distant terrain
uses Mapzen's Skadi elevation. Review actual renderer captures: [day](images/terrain/day.png),
[twilight](images/terrain/twilight.png), and [eclipse](images/terrain/eclipse.png).

See [source credits](../assets/terrain/ATTRIBUTION.md).
Measured geometry provides cliffs, ridges and valleys. Modeled vegetation,
weathered rock, meadows and snow depend on bare-earth slope and altitude.
Canopy slopes affect lighting, but cannot accidentally classify a tree as rock.
Relief is halved and the viewing elevation changes from 28 to 26 degrees to
keep the landscape beneath the celestial scene. A low idealized outcrop places
the observer above the lake rather than floating on its surface.

The offline baker projects each azimuth through a height-field visibility
envelope, interpolating the actual first intersected segment, including Earth
curvature. It stores angular albedo/coverage, normals/ambient occlusion,
16-bit horizontal distance and water coverage in three 4096x1024 RGBA maps.
Eight directional horizon angles are stored in two 1024x256 RGBA maps.
Full mip chains occupy about **66.7 MiB**. They upload once. There is no runtime
height-field ray march, terrain generation, geometry stream, shadow map or
second scene render.

The existing post pass relights surfaces from the moving sun, eclipses,
skylight and the giant's ambient phase estimate. Cached horizon angles give
soft terrain shadows interpolated across eight azimuths. Distance extinction
and the atmosphere LUT provide changing aerial perspective. Premultiplied
linear albedo/coverage uses sRGB texture storage and anisotropic mip filtering.

Lake normals use two small wind waves in world meters. Their independent phases
are reduced in double precision before upload, retaining continuity at Unix
scale and wrap boundaries. Derivative-based attenuation suppresses distant
wave aliasing. Fresnel reflection shows dry terrain, clouds and visible scene
radiance from the existing HDR buffer, with a solar highlight and absorbing
water color. There is no recursive reflection or full reflection framebuffer.

## Reproduce

Use an isolated Python environment with `scripts/terrain-requirements.txt`.
The source downloads are offline tools, never screensaver dependencies:

```sh
python scripts/fetch_terrain_data.py --output /tmp/terrain-swiss \
  --manifest assets/terrain/sources.json
python scripts/bake_terrain.py --source /tmp/N46E007.hgt.gz \
  --swiss /tmp/terrain-swiss --output assets/terrain
python tests/test_terrain_bake.py
```

The baker downloads the distant tile if missing and verifies its pinned SHA256.
Swiss downloads verify the committed immutable URLs and checksums. Python
wheels may require platform runtime libraries when run directly on NixOS.

## Limits

This is fixed-view surface geometry, not a walkable terrain world. Eight-angle
horizon interpolation approximates shadows between cached directions; fine
canopy self-shadowing is represented by normals and ambient occlusion. The
near-field projection uses a local LV95 affine approximation. Materials,
vegetation and lake extent are modeled, not ground-truth ecological data.
Reflection can reuse celestial detail only inside the original camera frustum;
outside it falls back to atmospheric radiance. Terrain reflections remain
available from the angular cache. This is not a claim of a full path-traced
landscape, survey-accurate vegetation, or physically exact hydrology.
