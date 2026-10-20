# Fragment cloud specialization

The browser's full-quality cloud cache used the same fragment program for the
coarse sky and the final scene. A uniform selected sky-only rendering, leaving
the compiler to retain the ground-edge, foreground fog and precipitation paths
in that program. Native compute shaders already compile their sky pass with a
constant selector.

An optional fragment variant now sets that same selector to a compile-time
constant for the sky cache. It expands the original `weather.frag` solver and
uses the existing framebuffer, pixel coordinates and cache dimensions. Weather,
lightning, quadrature, noise, lighting and image resolution remain dynamic and
unchanged. The regular fragment program still integrates the full scene,
including mixed terrain edges and foreground precipitation.

The shared C++ renderer selects the variant for WebGPU, WebGL 2 and native
fragment fallbacks. Native compute rendering is unchanged. Browser glue gains
no scene logic: the offline compiler emits specialized variants from the
same GLSL source, and checks its exact source identity. Separate programs need
separate uniform preparation; a failed optional compilation uses the original
single-program path.

The final full-resolution fragment pass also uses the native compute path's
[conservative empty-volume bounds](WEATHER_DISPATCH.md). A second optional
variant fixes the selector to the final scene and removes the lower-volume
marcher. Disjoint scissored fullscreen draws select this variant only for rows
proven to miss the lower layer; every other row retains the complete shader.
Fullscreen interpolation, pixel centres, both output attachments and all
non-volume effects are unchanged. Scissoring is disabled before the meteor and
composite passes, and the renderer restores its caller's scissor state.
Reduced cloud targets and optional-variant failures retain the complete solver.

The GPU regression compares daylight, sunset, storm, lightning and night
against a fixture that rejects only the optional fragment specializations. It requires
the fallback to run and limits complete-image differences to the existing
two-code/0.003-mean threshold. Full rendering checks and browser memory matrices
run in CI. Passing conversion or image tests does not establish a speedup or
the 30 fps hardware gate; those remain pending for this candidate.
The established compute/fragment comparison separately rejects only the new
empty-volume fragment variant. Its reference therefore still executes the full
marcher rather than sharing the candidate's row-selection decision.

For the earlier sky-only candidate `ead1700`, all five added full-scene software
comparisons have zero differing display codes, and WebGL/WebGPU/fallback/native
parity CI passes. Local builds and all 72 CPU/CLI checks pass for the subsequent
clear-row extension; its full rendering checks are pending. A compiler-only
probe using glslang 16.0.0 with `-V -Os` reduces generic weather SPIR-V from
97,828 bytes to 32,112 bytes for sky-only rendering and 47,448 bytes for an
empty-volume final pass. These code-size observations are not runtime timings.
