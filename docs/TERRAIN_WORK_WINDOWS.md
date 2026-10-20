# Terrain CPU windows

The controlled full-weather trial at `0e88c7a` still had seven forest deadline
misses in 1,420 steady frames. Every miss occurred with a pending terrain worker
and no upload. Smaller independent worker bursts had already been rejected:
they could fall behind flight and force synchronous terrain reconstruction.

The native renderer instead experiments with separating its own background
construction from GPU draws. A nonblocking GLES fence checks whether the
previous frame is finished. Only then does the render thread grant the worker
sixteen percent of the observed frame interval, clamped to 0.5–5.5 ms. The worker
acknowledges the end of the window at its existing cooperative checkpoints
before drawing starts. This is a scheduling change; mesh construction,
materials, lighting, image resolution and effects are unchanged.

The last CPU window and staged mesh uploads use separate frames. Existing
2 MiB upload chunks and resident coverage thresholds are unchanged. Seek and
shutdown release paused work before waiting, so they never need another frame
to complete. Invalid intervals use the nominal 30 fps budget; the upper bound
does not grow after a long pause. Cadence slower than 20 fps switches to
independent progress instead of starving construction between rare frames. A
failed fence also disables cooperative scheduling
and releases the worker. No GPU finish or blocking fence wait is added.

Software rasterizers retain the existing schedule: a slow CPU-rendered frame
cannot supply enough short windows to keep terrain moving. The browser also
retains its existing worker schedule and makes no new sync calls,
including through the WebGPU compatibility layer. Native Hyprlock packages
include the new helper explicitly.

Unit tests exercise progress, zero budgets, paused-worker exclusion, invalid
intervals, pause/resume bounds, completion, exception propagation, seeks and
shutdown. The rendering integration test advances frames during a pending
build, checks partial transfers and compares its completed geometry with a
fresh renderer.

The first prototype without the zero-contribution sampling shortcuts fell
behind on one coast flight and is rejected. A subsequent prototype trial
stopped on the reference run because monitoring exceeded its unchanged 10 ms
limit; no candidate performance result was obtained. Packaged full-weather
deadline validation is still required. The first packaged eight-percent setting
also failed: a forest build remained pending for 9.4 seconds and triggered a
306 ms synchronous rebuild. A CPU-only ABBA throughput probe built the same
512,800-vertex forest in 5.88 seconds with 2.67 ms windows, versus 2.49 seconds
with 5.33 ms windows at 30 Hz. The revised sixteen-percent grant restores
construction margin; it still needs the packaged deadline and streaming gate.
[Raw CPU window measurements](frame-drop-investigation/terrain-window-throughput.json).
CPU savings, image parity and passing functional tests alone do not establish
the 30 fps gate.
