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

## Deep-water material program

Already-classified deep-water blocks use a second surface program with the
water mask compiled to one. Every indexed vertex is below -64 m and at least
-1 °C; interpolation therefore keeps the general shader's water mask exactly
one across those triangles. The compiler can remove all land material,
canopy and terrain-shadow code from that program. Coastlines, frozen water,
land, shadow maps and the outer horizon band retain the general program.
Draw ranges, geometry, wave filtering and water lighting stay unchanged.
There is one additional cached program, no new texture or render target.

Three alternating 90-frame hidden-preview comparisons against `f977724`, with
the same first ten frames excluded from both versions, measured ocean medians
11.792 → 10.692 ms (9.3% lower) and coast 14.746 → 14.265 ms (3.3% lower).
All six optimized steady runs stayed below 33.33 ms; the general shader had
one 44.53 ms coastal frame. These are optimization diagnostics, not acceptance
against frozen Phase 1 or physical presentation. The [measurement report](landscape-water-specialization-benchmark.json)
retains every run, binary identity and command, including that slow reference.

## Whole-mesh inland material program

Meshes whose vertices are all at least 1 m above sea level use a cached land
program. The shoreline mask is zero throughout these triangles; only the final
zero-weight ocean blend is compiled out. The early water branch is retained so
fragment helper lanes and derivatives follow the general program. Every vertex
in the entire mesh must qualify. Mixed coastal meshes, ocean, shadow maps and
optional reflected terrain retain their existing programs and draw batches.
The classification is computed during the existing mesh build and swaps with
that mesh. It adds one program and one boolean, with no new texture, render
target, geometry, upload or per-frame terrain sampling.

A per-block variant failed the unchanged image gate at the coast (9 display
codes peak versus the allowed 2) and was discarded. Moving the classification
to the entire mesh preserves the general shader at the shoreline. Complete AMD
images are now pixel-identical for all four fixtures with both cloud models;
Mesa passes the same existing tolerance. CPU checks cover inland vertices,
mixed coasts, ocean rejection and cooperative mesh construction.

Three alternating 90-frame diagnostics against `58cd2bc`, excluding the same
first ten frames from each version, measured forest medians 14.149 → 12.511 ms
(11.6% lower), snow 12.357 → 11.992 ms, coast 17.397 → 17.529 ms and ocean
11.802 → 11.610 ms. This shared-desktop diagnostic does not establish a coastal
gain or final budget acceptance. It retained 10 reference and 11 optimized snow
deadline misses, plus 25 reference ocean misses. The [full report](landscape-inland-specialization-benchmark.json)
retains every run and binary identity. The original Phase 1 acceptance target
below remains unchanged.

## Verification

`weather-specialization-tests` renders against a temporary reference shader
that restores the original uniform branch, with identical numerical assets.
It compares complete images across ocean, coast, forest and snow at daylight,
twilight and night phases, plus normal weather and the historic fixed view.
The reference fixture also retains the general interpolated water mask.
CPU tests verify pure-liquid bounds for every specialized water triangle.
AMD hardware comparisons were pixel-identical across all fixtures.
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
