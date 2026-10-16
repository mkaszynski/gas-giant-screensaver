# Biome weather beta

For the combined branch and its latest measurements, see the
[weather integration review](WEATHER_INTEGRATION.md). The landscape comparison
below records the earlier weather beta.

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
  structure. A startup GPU filtering probe selects a compact RGBA8 mip chain (about 1.14 MiB)
  when interpolation is sufficiently smooth, or RGBA16F (about 2.3 MiB) on
  implementations with quantized UNORM filtering. Both paths use the same data.
  It is built once per renderer, uploaded once, and sampled during animation; the noise field is
  never regenerated during animation. Double-precision CPU offsets keep
  animation smooth at Unix-scale times.
* Cloud composition and terrain clipping use display resolution (1920×1080 at 1080p).
  A same-frame half-resolution cache reuses low-cloud radiance and transmission
  only across smooth sky regions. Ground, partial MSAA coverage, bright unresolved
  sources and steep cloud transitions retain direct display-pixel integration.
  Thin high clouds, fog and precipitation remain at display resolution. The cache
  adds about 4 MiB at 1080p; it has no temporal history or delayed lighting.
  `--no-weather-cache` traces every ray for comparison. Both paths use
  12–48 deterministic samples according to the distance through the layer
  (a 150 m target interval, bounded by that range). Detail lookups are skipped
  only where the density upper bound proves both the body and anvil are empty.
  Skylight and direction-dependent lighting terms are prepared once per ray;
  only solar transmission and local extinction vary along it. This retains the
  same density and lighting without repeated sky lookups and phase calculations.
  Fine sampling is retained in transparent
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

## Review gallery

These are actual 1920×1080 GPU captures. Flight and weather times are independently
controlled; the storm and flash frames deliberately place the observer inside a
mature cloud. The low-bank scenes leave the giant visible above the weather.

| View | Capture | Sky seconds | Flight seconds | Weather seconds |
| --- | --- | ---: | ---: | ---: |
| Forest cloud banks | [Daylight](weather/forest-cloudbanks.png) | 510 | 162000 | 1790900000 |
| Warm low sun | [Sunset](weather/forest-sunset.png) | 900 | 162000 | 1790900000 |
| Ocean and reflected rings | [Ocean](weather/ocean.png) | 1740 | 0 | 0 |
| Cold mountains and thin high cloud | [Snow](weather/snow.png) | 360 | 210600 | 1790900000 |
| Mature storm | [Storm](weather/storm.png) | 510 | 162000 | 1260 |
| Before a nearby flash | [Night](weather/night-before.png) | 1200 | 162000 | 1512.3 |
| Intracloud illumination | [Flash](weather/night-flash.png) | 1200 | 162000 | 1512.8 |
| Cloudy coastal view | [Coast](weather/coast.png) | 1740 | 5600.390625 | 1790900000 |

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

Terrain-only culling remains pixel-identical. With weather composition, the test
permits one 8-bit level in at most 16 channels of the 640×360 image: equivalent
lighting math and half-float distance resolves can straddle a rounding boundary.
A separate comparison against the original volume-lighting calculation bounds
its error to one level and less than .002 average across daylight, sunset, night,
storm and coast. The terrain MSAA fixture retains its analytical coverage check.

The performance target remains 30 fps at 1920×1080; the measurements below
include timing tails and deadline misses rather than treating a good median
as a complete performance pass.

## Measured beta performance

Measured source: [`62177b0`](https://github.com/mkaszynski/gas-giant-screensaver/commit/62177b02d75c2754c0463f3fc7f84314429d2701).
The reference is the reviewed landscape package from `c7adb00` (the merged
landscape tree). All measurements use one hidden 1920×1080 rendering target,
30 fps pacing and automatic GPU clocks on this machine's AMD integrated GPU.
They synchronize CPU/GPU rendering and exclude compositor presentation.
Both desktop outputs were asleep; combined multi-monitor presentation cost is
not measured. Three alternating pairs of 180 frames cover each scene. The first
10 frames of each run are excluded from the steady figures; later stalls remain.

| View | Landscape median (ms) | Weather median (ms) | Weather p95 (ms) | Missed 33.3 ms deadlines |
| --- | ---: | ---: | ---: | ---: |
| Forest banks | 13.19 | 19.09 | 33.58 | 27/510 |
| Forest overcast | 13.21 | 18.75 | 28.88 | 24/510 |
| Forest storm | 13.24 | 15.80 | 18.75 | 17/510 |
| Ocean | 12.24 | 23.69 | 35.75 | 29/510 |
| Snow | 11.78 | 17.30 | 21.62 | 18/510 |
| Cloudy coast | 15.16 | 36.58 | 42.33 | 510/510 |

The 30 fps budget is **not fully met**: the cloudy coast remains above 33.3 ms,
and other scenes have intermittent long frames. The single-run improvements do
not establish a locked 30 fps result. Terrain-stall investigation continues in
[issue 13](https://github.com/mkaszynski/gas-giant-screensaver/issues/13); these
measurements do not attribute every miss to terrain streaming.

The 720-frame forest run measured 20.81 ms steady median, with 124/710
steady frames over budget and 59.20 ms maximum. Peak process RSS was 372.4 MiB;
RSS excludes some GPU residency. Normal streamed uploads remained bounded at
2 MiB per frame. Weather textures/caches have fixed dimensions and no history
that grows with animation time.

[Complete paired report](weather/benchmark.json),
[raw frame records](weather/frames), and [memory-run summary](weather/memory.json)
retain the measured tails, startup cost, binary/shader/source hashes and display
power state. Reproduce the paired run with:

```sh
python3 scripts/benchmark_weather.py --baseline result-landscape-phase2 --output /tmp/weather-benchmark.json --frames-dir /tmp/weather-frames
```

## Physical references

Cloud forms and qualitative altitude structure follow NOAA's
[ten basic clouds](https://www.noaa.gov/jetstream/clouds/ten-basic-clouds) and
[cloud classification chart](https://psl.noaa.gov/outreach/education/pdf/noaa-cloud-poster.pdf).
Most real lightning occurs within clouds; see
[NWS Melbourne's explanation](https://www.weather.gov/mlb/what_is_lightning).
These sources inform morphology and behavior, not the numerical weather schedule
or a claim of a complete physical atmosphere simulation.
