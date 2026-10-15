# Biome weather beta

Weather follows the airborne observer's regional moisture, temperature, elevation
and ocean fraction. Five nearby world samples avoid switching weather at a single
ridge or shoreline. Warm humid regions favor cumulus and thunderstorms; cool wet
regions favor stratus and fog; arid regions usually have clear air and higher,
sparser cloud. Fair-weather banks sit low enough to be viewed from above on much
of the flight. Convective towers can reach the upper sky and hide the giant.

Continuous world-space fronts evolve independently of the 1,800-second solar day.
Neither a new solar day nor a completed three-day flight resets the weather.
Startup uses the current Unix clock. `--weather-time` selects a reproducible
weather instant without changing the sun, moons or flight position. Debug seeking
advances weather along with the selected scene time; pausing freezes both.

## Rendering

* A periodic 64³ RGBA noise volume combines smooth value noise and cellular
  structure. Its floating-point GPU mip chain occupies approximately 2.3 MiB. It is built once per
  renderer, uploaded once, and sampled during animation; the noise field is
  never regenerated during animation. Double-precision CPU offsets keep
  animation smooth at Unix-scale times.
* The cloud volume is integrated at display resolution (1920×1080 at 1080p),
  using at most 48 deterministic samples. Fine sampling is retained in transparent
  skins and silhouettes; steps grow smoothly inside opaque bodies. The distant
  high cloud layer takes two filtered noise lookups. There is no frame-random
  noise or temporal history to trail across the moons.
* A 128×128×32 RG16F lighting volume (2 MiB) stores direct and multiple-scattering
  solar transmission over the nearest 32 km. Two up-sun probes are evaluated per
  voxel each lit frame, instead of repeatedly for every view sample. It filters
  transmission rather than optical depth to preserve energy across cloudy/clear
  voxels. Fine local extinction remains at display resolution. A smooth blend
  switches to direct probes outside the cache. Nights and total eclipses skip
  the solar cache. There is no stepped temporal light update.
* Stable spherical-shell intersections place clouds above, below or around the
  camera. Terrain supplies a coverage-weighted linear distance buffer, resolved
  with the same MSAA weights as its radiance. This retains foreground distance
  even when a depth resolve would choose an uncovered sample. The exact sea
  sphere also clips ocean rays, avoiding water-LOD curvature artifacts.
* Cloud and terrain pixels share display resolution. Most pixels require one
  weather result; partially covered ridge/foliage pixels retain separate
  foreground and background weather. Their original MSAA coverage combines the
  two paths, preserving antialiasing without clouds leaking through solid land.
  The two RGBA16F weather targets occupy approximately 31.6 MiB at 1080p.
  Their alpha channel stores transmission directly, retaining precision through
  dense clouds instead of rounding near-total opacity to opaque/transparent steps.
* Lighting uses the existing atmospheric transmission/sky tables for solar and
  sunset colors, an approximate droplet phase function, multiple-scattered light,
  and phase-dependent light from the giant. Additional dry-air attenuation blends
  distant clouds into the atmosphere. No separate cloud sunset color preset is
  used. Clouds attenuate the giant, moons, stars, solar disk and solar halo.
* Solar ground shadows and water reflections sample the same density field.
  Shadow columns and reflected cloud lighting use inexpensive approximations,
  rather than a second full volume march for every terrain fragment. Storms also
  increase filtered water slopes and darken/wet unfrozen land materials.
* Four height samples integrate shallow fog and distant precipitation curtains.
  Curtains descend below their parent cloud columns; no raindrops or snowflakes
  are drawn as giant particles near an airborne camera. At this viewing distance
  rain and snowfall are both approximated by their volume extinction.
* Nearby lightning is primarily intracloud light. Irregular, brief, two-stroke
  pulses illuminate a localized cloud region rather than the whole screen. Pulse
  energy is integrated over each frame, preserving subframe flashes. A mature
  warm storm attempts one event per 24 display seconds, with visibility depending
  on cloud density, terrain, camera direction and daylight. Clear/cold conditions
  produce no local lightning. This is separate from the giant's accelerated
  lightning and its existing brightness control.

The weather is a deterministic visual climate model, not a forecast solver. Front
motion, storm frequency, multiple scattering, fog and optical emission are bounded
approximations chosen for this accelerated fictional system. Local lightning is
not calibrated to a camera's absolute photometry. Cloud shadows do not add a new
full-resolution shadow-map pass; arbitrary reflections are approximate. These
limits must be assessed in actual daytime, twilight, night and storm images.

## Review commands

```sh
nix build .#gas-giant-screensaver -o result-weather
./result-weather/bin/gas-giant-screensaver --data-dir ./result-weather/share/gas-giant-screensaver --fullscreen --debug --fps 30 --time 510 --travel-time 162000 --weather-time 1790900000
```

Use `--weather-time 1260` at the same location for a developed storm. Use
`--no-weather` to compare the previous single thin-cloud sheet. This comparison
also disables weather-driven ground wetness and wave changes. `--no-emissions`
disables the giant's emissions, leaving local weather unchanged. `--fixed-view`
retains the original stationary landscape/cloud path for astronomy comparisons.

## Validation

`weather-tests` checks climate distributions, warm/cold convection, independent
daily variation, continuity across the longitude seam and at Unix-scale times,
flash exposure energy and bounded noise storage. `weather-render-tests` checks
real cloud visibility, lightning, smooth subframe motion, cached/direct lighting,
fixed/adaptive quadrature, and homogeneous slabs
above/below the observer and hidden behind opaque terrain. The slab fixtures
isolate depth clipping from cloud morphology. A cloud-backed ridge fixture
compares the antialiased result with both analytical coverage and the old broken
composite. The existing terrain/sky tests
continue to exercise antialiasing, opacity, shadows, reflections and streaming.

The performance target remains 30 fps at 1920×1080. Paired measurements against
the reviewed landscape build must include clear, cloudy, cold and storm cases,
plus p95/max and missed deadlines; a median below 33.3 ms alone is insufficient.
Previous terrain frame stalls are tracked separately in
[issue 13](https://github.com/mkaszynski/gas-giant-screensaver/issues/13).

## Physical references

Cloud forms and qualitative altitude structure follow NOAA's
[ten basic clouds](https://www.noaa.gov/jetstream/clouds/ten-basic-clouds) and
[cloud classification chart](https://psl.noaa.gov/outreach/education/pdf/noaa-cloud-poster.pdf).
Most real lightning occurs within clouds; see
[NWS Melbourne's explanation](https://www.weather.gov/mlb/what_is_lightning).
These sources inform morphology and behavior, not the numerical weather schedule
or a claim of a complete physical atmosphere simulation.
