# Native rendering policy

Animated uniforms can make Mesa RadeonSI compile several specialized graphics
shaders during animation. Those background compilers share CPU, memory and
power with the integrated GPU. The frame-drop investigation observed about
half a CPU-second of optimized compilation after the first measured frame,
with spikes even when no terrain build or upload was active.

The native preview and packaged Hyprlock now default the **process-local** Mesa
option `radeonsi_inline_uniforms` to `false`, before graphics initialization.
Normal shader optimization and all rendering effects remain enabled. Explicit
operator overrides take priority; other drivers ignore this RadeonSI option.
Browser WebGL/WebGPU code and system clock/power settings are unchanged.

The [four-run diagnostic](frame-drop-investigation/compiler-diagnostic.json)
changed only this option, keeping the same binary and assets. Optimized shader
compiler activity fell to zero; enabled runs missed 6 and 17 of 180 deadlines,
and disabled runs missed 1 and 0. Their medians were similar. These are small
correlation diagnostics with CPU thread sampling, **not** controlled performance
acceptance. The individual frame CSVs and compiler CPU timelines are retained
alongside the report. A disabled run still exceeded 33.33 ms.

[Image comparisons](frame-drop-investigation/native-policy-images.json) check
eight deterministic frames each of forest day/night lightning, coastal sunset,
ocean storms and snow day/night. The forest reference is 1920×1080; the other
scenes are 640×360. All 48 comparisons have zero differing display codes.
Unit tests verify the default and preservation of explicit operator values.

For a diagnostic comparison, prefix the normal native command with
`radeonsi_inline_uniforms=true`. This restores Mesa's previous specialization
policy for that process. Do not replace an operator-provided value globally.

Implementation rationale follows Mesa 25.2.6's RadeonSI sources (`si_pipe.c`
driconf option and `sh`/`sh_opt` queues; `si_state_shaders.cpp` optimized variants).
Compute shaders do not use these uniform-specialized variants: the issue is in
the graphics pipeline, despite similarly named compiler statistics on compute
programs.

PR #30 remains a draft. Sustained 30 fps, native presentation, long-run memory,
and the unchanged frozen Phase 1 cost comparison still need final validation.
Green image tests alone do not establish those performance gates.
