# Terrain CPU windows

The controlled full-weather trial at `0e88c7a` still had seven forest deadline
misses in 1,420 steady frames. Every miss occurred with a pending terrain worker
and no upload. Smaller independent worker bursts had already been rejected:
they could fall behind flight and force synchronous terrain reconstruction.

The native renderer instead experiments with separating its own background
construction from GPU draws. A nonblocking GLES fence checks whether the
previous frame is finished. Only then does the render thread grant the worker
eight percent of the observed frame interval, clamped to 0.5–3 ms. The worker
acknowledges the end of the window at its existing cooperative checkpoints
before drawing starts. This is a scheduling change; mesh construction,
materials, lighting, image resolution and effects are unchanged.

The last CPU window and staged mesh uploads use separate frames. Existing
2 MiB upload chunks and resident coverage thresholds are unchanged. Seek and
shutdown release paused work before waiting, so they never need another frame
to complete. Invalid intervals use the nominal 30 fps budget; the upper bound
also applies after a long pause. A failed fence disables cooperative scheduling
and releases the worker. No GPU finish or blocking fence wait is added.

The browser retains its existing worker schedule and makes no new sync calls,
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
deadline validation is still required. CPU savings, image parity and passing
functional tests alone do not establish the 30 fps gate.
