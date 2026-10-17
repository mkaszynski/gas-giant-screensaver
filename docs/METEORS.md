# Meteor showers

Natural mode schedules fictional encounters with cometary debris every 1–8
real hours, averaging 4.5 hours. Each encounter lasts 6–16 minutes with a soft
rise/fall in the arrival rate. This cadence compresses shower encounters for a
screensaver; it does not reproduce Earth's annual meteor calendar. A shower can
be missed in daylight, beneath clouds or with its radiant below the horizon.

The debug menu offers Auto, Preview now and Off. The frequency slider is
logarithmic, from .5 to 60 bright visible-field candidates per minute; normal
mode starts at 3. Preview now resets the stream epoch and gives its first
candidate after .55 seconds. The night gate still applies and the menu says
"Waiting for dark sky" when appropriate. The slider changes the event rate,
not the speed of individual meteors. A fresh launch restores defaults.

## Appearance and geometry

Events begin on a spherical 105–115 km ablation shell and move at 26–68 km/s.
A shared inertial radiant has a small angular dispersion. The sampled field is
stored in moon coordinates: camera movement changes its projection instead of
moving the meteors with the screen. The observer's altitude, the moon's spin
since birth and the changing local basis enter that transform. This is a visual
ablation model, not a gas-dynamics simulation.

The modeled bright population has magnitudes +2 to -4, using the existing
catalog-star flux zero point and contrast adaptation. The rate describes those
bright candidates; fainter dust is omitted. Most heads are close to white with
weak speed-dependent emission color. Atmospheric extinction reddens grazing
streaks, and the nearby sunlit giant can wash them out. A selected brighter
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
apply. They are in front of the distant giant, moons and rings. Gaussian line
convolution, a pixel-footprint term and exposure integration avoid hard-edged,
frame-dependent streaks. The tails are not photographic textures or noisy
particle clouds.

## Cost and verification

The natural encounter clock is an O(1) query at arbitrary Unix timestamps;
consecutive jittered starts have a proven 1–8 hour separation. Only recent
candidates are inspected. Work and storage are bounded to 96 line segments
(23 KiB of reusable vertex storage); no extra render target, fullscreen pass or
texture is added. An inactive effect issues no GPU work. The first active batch
lazily creates its shader and buffer, and later batches reuse them.

CPU tests cover scheduling, bounds, night-only behavior, rate changes,
frame-rate-independent exposure energy, physical altitude and observer movement.
GPU tests check visible night output, zero daytime output, optical stability
under quarter-pixel camera shifts, restoration after disabling, the airborne
HDR path and extinction by a deliberately opaque cloud volume. The actual
preview interaction test selects meteor modes and drags the frequency slider;
CI also runs these tests with Mesa software rendering.

For a repeatable night review:

```sh
nix run . -- --fullscreen --debug --time 1254 --travel-time 162000 --meteor-mode preview --meteor-preview-start 1254 --meteor-rate 30
```

A selected bright frame (the scene and event time must both advance):

```sh
nix run . -- --capture /tmp/meteor.png --time 1254.9 --travel-time 162000 --weather-time 1790900000 --meteor-mode preview --meteor-preview-start 1254 --meteor-rate 30 --width 1920 --height 1080
```

Use `--meteor-mode off` for its exact meteor-free comparison. The main benchmark
supports the same options for matched complete-renderer measurements. Render
completion time and native surface submission cadence are different metrics;
neither a screenshot nor a hidden-window average proves physical presentation
at a given frame rate. Review measurements and remaining limitations are recorded
in the feature PR before merge. The NixOS input is not changed by this feature.
