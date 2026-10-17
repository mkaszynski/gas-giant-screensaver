# Compile the selected cloud model

`SurfaceOptions::weather` is fixed when a renderer is constructed. Previously,
terrain, water reflections and final compositing retained both cloud models
behind the `uWeatherEnabled` uniform. Those large shader branches could keep
unneeded instructions and temporary storage in the compiled program.

The surface and composite programs now define `OBSERVATORY_WEATHER_MODE` as
0 or 1 before shader compilation. The shared weather include makes the model
selection constant, letting the compiler discard the unused model. Weather
parameters (coverage, storms, fog, precipitation, wind and lighting) remain
uniforms and continue to change normally. Numerical fields, geometry,
material resolution, filtering and coverage samples are preserved.

Normal airborne rendering selects volumetric weather. `--no-weather` retains
the original thin clouds and their shadows/reflections, matching Landscape
Phase 2's original scope. Fixed-view rendering also uses thin clouds. The
optional terrain-reflection program receives the same selection as terrain.

The thin-cloud profile also omits the unused R16F distance attachment and its
resolved texture. These exist only for volumetric weather's atmosphere/terrain
intersection, so that profile still allocates and resolves them. Thin clouds
avoid the extra multisample storage, shader output and resolve blit. Terrain
depth and filtered radiance use their existing targets.

## Verification

`weather-specialization-tests` renders against a temporary reference shader
that restores the original uniform branch, with identical numerical assets.
It compares complete images across ocean, coast, forest and snow at daylight,
twilight and night phases, plus normal weather and the historic fixed view.
Initial AMD hardware comparisons were pixel-identical across all fixtures.
CI runs the same comparison on Mesa, alongside the existing physical water,
terrain, weather, shadow, silhouette and host-state regression suites.
The distance-target check exercises both cloud profiles through a framebuffer
resize, verifying that the optional texture and attachment follow the selected
profile and that both resolve paths report no GL errors.

This is a candidate optimization, not completed performance acceptance. The
original frozen Phase 1 reference remains `f882d4d701308b06c25be0a2118871dc244f9794`.
Repeated, matched complete-renderer runs must demonstrate both median and p95
within 1.30 times that reference, and rendering within the 33.33 ms deadline at
1920×1080. Native surface submission cadence and bounded memory also require
separate verification. Keep prior failed measurements and contaminated runs.
A locked desktop rendering another observatory is unsuitable for acceptance.

A coastal fixture using the original thin-cloud model:

```sh
nix run . -- --benchmark 180 --fps 30 --width 1920 --height 1080 --time 1740 --travel-time 5600.390625 --weather-time 1790900000 --no-weather --benchmark-output /tmp/coast.csv
```

Use the same command, immutable assets and binary identity for each baseline
or candidate run; alternate their order across repeats. Repeat for the ocean,
forest and snow fixtures in `LANDSCAPE_PHASE2.md`. Native rendering with normal
weather remains a separate default-profile check. Neither software-renderer
CI nor a screenshot proves physical 30 fps presentation.
