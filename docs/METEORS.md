# Meteor showers

Natural mode schedules fictional encounters with cometary debris every 1–8
real hours, averaging 4.5 hours. Each encounter lasts 6–16 minutes with a soft
rise/fall in the arrival rate. This cadence compresses shower encounters for a
screensaver; it does not reproduce Earth's annual meteor calendar. An automatic shower can
be missed in daylight, beneath clouds or with its radiant below the horizon.

The debug menu offers Auto, Preview now and Off. Two logarithmic sliders separate encounter frequency from activity inside a
shower. **Mean gap** selects .5–12 hours, with the default 4.5-hour mean giving
random gaps of 1–8 hours; the displayed random range scales with that setting.
Very short debug intervals shorten encounters to avoid overlapping them.
**Meteors** selects .5–1800 bright visible-field candidates per minute within a
shower, default 3. Preview now resets the stream epoch and gives its first
candidate after .55 seconds. An explicit trigger is a debug visibility override:
it renders in daylight and through cloud cover, with a manual contrast override even beside the sunlit giant.
Terrain remains opaque. Auto retains its night gate and physical cloud extinction.
The menu labels the manual override "Manual shower - all skies". The controls change event rates,
not the speed of individual meteors. Manual activity uses a separate, pause-aware clock: forward/backward sky seeks re-anchor the visible field and restart the local shower without jumping meteor ages. Meteors continue at a timeline endpoint while Play is enabled; Pause still freezes them. A fresh launch restores defaults.

## Appearance and geometry

Events begin on a spherical 105–115 km ablation shell and move at 26–68 km/s.
A shared inertial radiant has a small angular dispersion. The sampled field is
stored in moon coordinates: camera movement changes its projection instead of
moving the meteors with the screen. The observer's altitude, the moon's spin
since birth and the changing local basis enter that transform. This is a visual
ablation model, not a gas-dynamics simulation.

The modeled bright population has magnitudes +2 to -4, using the existing
catalog-star flux zero point and contrast adaptation. Event brightness also controls ablation depth and wake persistence: faint short strokes and brighter long tails coexist, with modest seeded variation. Their cores remain optically unresolved. The rate describes those
bright candidates; fainter dust is omitted. Most heads are close to white with
weak speed-dependent emission color. Atmospheric extinction reddens grazing
streaks, and the nearby sunlit giant can wash out automatic showers. Manual review bypasses that contrast suppression. A selected brighter
meteor is useful for reviewing the geometry; a typical frame is much quieter.

Heads use an exact exposure integral of their ablation envelope and a
motion-blurred optical profile. A short wake fades behind them. Some bright
meteors leave a weak ion train in the deposited air, with differential wind
shear. These features use real seconds despite the accelerated celestial sky.
They are not permanent glowing lines or a fullscreen flash. The [American
Meteor Society's fireball FAQ](https://www.amsmeteors.org/fireballs/faqf/) informs
the speed range, emission colors, short luminous lifetime and ion-train behavior.

Streaks enter the HDR scene ahead of the terrain/weather composite, so normal
atmospheric transmission, cloud extinction, opaque terrain and water reflection
apply to automatic showers. Manual triggers also draw the cloud-blocked fraction
into the existing weather buffers using destination transmission; this adds one
small streak draw, no target or fullscreen pass. Partial terrain silhouettes
retain their sky coverage. They are in front of the distant giant, moons and rings. Gaussian line
convolution, a pixel-footprint term and exposure integration avoid hard-edged,
frame-dependent streaks. The tails are not photographic textures or noisy
particle clouds.

## Cost and verification

The natural encounter clock is an O(1) query at arbitrary Unix timestamps;
consecutive jittered starts have a proven 1–8 hour separation. At most 191 recent
candidates are inspected, even at 1800/min and the longest supported exposure. Work and storage are bounded to 1024 line segments
(240 KiB of reusable vertex storage); no extra render target, fullscreen pass or
texture is added. An inactive effect issues no GPU work. The first active batch
lazily creates its shader and buffer, and later batches reuse them.

CPU tests cover scheduling, bounds, automatic night gating, manual daylight triggers, rate changes,
frame-rate-independent exposure energy, physical altitude, observer movement, maximum-rate capacity, size/brightness diversity and forward/backward/endpoint seeks.
GPU tests check visible manual output in night and daylight, zero automatic daytime output, optical stability
under quarter-pixel camera shifts, restoration after disabling, the airborne
HDR path and manual visibility through a deliberately opaque cloud volume. The actual
preview interaction test selects meteor modes, drags both frequency controls and verifies actual meteor pixels after large forward/backward and endpoint seeks;
CI also runs these tests with Mesa software rendering.

For a repeatable night review:

```sh
nix run . -- --fullscreen --debug --time 1254 --travel-time 162000 --meteor-mode preview --meteor-preview-start 1254 --meteor-rate 30
```

A selected bright frame (the scene and event time must both advance):

```sh
nix run . -- --capture /tmp/meteor.png --time 1254.9 --travel-time 162000 --weather-time 1790900000 --meteor-mode preview --meteor-preview-start 1254 --meteor-rate 30 --width 1920 --height 1080
```

Use `--meteor-mode off` for its exact meteor-free comparison.
`--meteor-interval HOURS` sets the mean gap between natural encounters. The main benchmark
supports the same options for matched complete-renderer measurements. Render
completion time and native surface submission cadence are different metrics;
neither a screenshot nor a hidden-window average proves physical presentation
at a given frame rate. Review measurements and remaining limitations are recorded
in the feature PR before merge. The NixOS input is not changed by this feature.
