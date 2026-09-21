# Gas Giant Observatory

A fixed view from a green mountain ridge on a synchronously rotating moon.
A large gas giant hangs above the mountains while twenty eccentric moons move
through a shared 3D system. Slender, translucent ice rings share the giant’s
tilted equator. Sunlight, phases and eclipse shadows follow that
geometry. The sky changes through a 30-minute solar day, with thin drifting
clouds and stars emerging at dusk.

![Twilight from the observer moon](assets/preview-twilight.png)

[Animated preview](assets/preview.webp) · [MP4](assets/preview.mp4) · [Daylight](assets/preview-day.png) · [Eclipse](assets/preview-eclipse.png)

## Run on NixOS / Hyprland

```sh
nix run github:mkaszynski/gas-giant-screensaver/feature/icy-rings -- --fullscreen
```

Press **Escape** or **Q** to close. The preview does not lock the desktop.
It caps rendering at 30 fps and stops when Hyprland reports a display powered
off. Use `--fps 15` for a lower frame budget, or `--timeout 20` for a brief preview.

From a checkout:

```sh
nix run . -- --fullscreen --timeout 20
```

## Native lock screen

The optional package uses Hyprlock's native session lock and PAM authentication.
It retains the cyan password segments, Orbitron status text, red failure halo,
three-second failure hold, and 800 ms deceleration plus 1000 ms unlock fade of
the Hyperspace Starfield setup. Rendering pauses when the compositor stops
sending frames to an inactive output.

Validate the config without locking:

```sh
nix run .#gas-giant-lock -- --check-config
```

Activate the actual lock (unlock with your normal account password):

```sh
nix run .#gas-giant-lock
```

For Home Manager, add this repository as a flake input, select
`inputs.observatory.packages.${pkgs.stdenv.hostPlatform.system}.hyprlock-observatory`
as `programs.hyprlock.package`, and use `hyprlock/observatory.conf` as the lock
configuration. NixOS needs `security.pam.services.hyprlock = {};` as usual.
Installing the preview does not change any host configuration or idle timeout.

## Appearance and geometry

- Giant: Jupiter radius, three Jupiter masses; solar-size star at about 1 AU.
- Observer orbit: semimajor axis **three giant radii**, eccentricity **1/9**.
  Apocenter is exactly **25% farther** than pericenter.
- The observer moon has Earth's **6,371 km radius**. The other nineteen moons
  retain **three times their initial radii**; the largest is about three Earth
  radii. This is a fictional, artist-directed system.
- Latitude 38° north, longitude 50° from the mean subplanet meridian. Mean
  synchronous rotation preserves natural eccentric-orbit libration.
- Twenty moons includes the observer moon. Visibility and apparent size follow
  geometry; the renderer does not arrange all nineteen others in the frame.
- Rings: **1.235–1.780 giant radii**, with narrow bands and a Cassini-like gap.
  Their plane and the giant’s texture share a **26.7° tilted pole**. Optical
  depth varies by radius and viewing angle; moons remain visible through
  thinner bands. Planet/ring and moon/ring shadows work in both directions.
- Faint nighttime airglow and ambient mountain light preserve subtle detail.
- Finite solar disk (about 0.53° across), atmospheric aureole, soft optical glare,
  mutual eclipses, and planetshine approximation.
- Photographic-style generated mountain albedo and giant band textures; runtime
  lighting supplies their changing appearance. Mountain interiors fully occlude
  the sky, with antialiasing restricted to the silhouette.

## Performance

The renderer is C++20 / OpenGL ES 3.0. A cached transmittance table and an
isotropic multiple-scattering table feed a small sky lookup texture. Only the
sky table changes during the day. Sphere draws are bounded to screen rectangles;
impossible shadow casters are rejected on the CPU. Clouds are thin texture
layers, and solar glare adds no extra rendering pass. There is no runtime AI,
network access, volumetric cloud marching, or per-frame texture upload.

Measurements and limitations: [validation report](docs/VALIDATION.md).
The frame cap and output suspension matter more for power than peak FPS.

## Build and test

```sh
nix develop
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/render-tests
./build/ring-render-tests
nix flake check
```

Use `--no-rings` for a direct visual or performance comparison.

GPU tests need a display and GLES 3. CI runs them using Mesa llvmpipe and Xvfb.
They check opaque mountain occlusion, deterministic resize, visible bodies,
and day/night contrast. Unit tests verify orbital geometry and eclipse cases.

Capture and measure the real renderer:

```sh
nix run . -- --capture twilight.png --time 880 --width 1920 --height 1080
nix run . -- --benchmark 180 --time 1350 --width 1920 --height 1080
```

`--time` sets the starting scene time; `--capture` freezes it. Normal launches
continue from wall-clock-derived celestial time. `--day-seconds` changes the
speed of the system. See `--help` for recording options.

## Scope and provenance

This is a purpose-built screensaver, not an orbital habitability simulator.
Keplerian paths are prescribed; long-term N-body stability is not claimed.
Mountains are a fixed photographic-style layer, not navigable terrain.
Atmospheric eclipses use local visibility rather than a volumetric shadow
integration; overlapping penumbras use an approximation. See
[design](docs/DESIGN.md) and [asset provenance and prompts](docs/ASSETS.md).

Original renderer: MIT. Hyprlock integration incorporates BSD-3-Clause upstream
code and attributed UX patches. Generated assets: CC0-1.0 to the extent rights
exist. See [LICENSE](LICENSE) and [attribution](docs/ASSETS.md).
