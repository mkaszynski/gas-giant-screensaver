# Fragment cloud cache specialization

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
no scene logic: the offline compiler emits one additional variant from the
same GLSL source, and checks its exact source identity. Separate programs need
separate uniform preparation; a failed optional compilation uses the original
single-program path.

The GPU regression compares daylight, sunset, storm, lightning and night
against a fixture that rejects only the optional specialization. It requires
the fallback to run and limits complete-image differences to the existing
two-code/0.003-mean threshold. Full rendering checks and browser memory matrices
run in CI. Passing conversion or image tests does not establish a speedup or
the 30 fps hardware gate; those remain pending for this candidate.
