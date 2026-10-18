# Preview frame deadlines

The standalone preview previously slept until each actual frame start plus its
period. Timer overshoot accumulated on every iteration, giving about 29.85
completed swaps per second with a 30 fps cap. It now uses the shared steady-clock
`FramePacer` deadline. A late wake-up shortens the next sleep; an overrun or idle
resume starts a fresh cadence without queuing missed frames. The cap stays 30.
Rendering expressions, resolution, effects and debug quality are unchanged.

The native widget continues to use `FramePacer::delay`, which wraps the same
deadline in its existing minimum-one-millisecond event-loop timer. Its timer
rounding and overrun behavior are unchanged. The new unit tests simulate 20
seconds at 30/60 fps with repeated wake-up delay, a slow frame and an idle resume,
in addition to the existing native timer tests.

Eight fullscreen 1920×1080 runs used relative/absolute/absolute/relative ordering
for forest and coast, with default volumetric weather and the debug HUD enabled.
Both binaries share renderer objects, compiler, GLFW and immutable data. A small
interposer records swaps and sleep calls in memory and writes after exit; it
adds no GPU query or synchronization. Every raw row and workload sample is in
[preview-frame-pacing](preview-frame-pacing/). Three predeclared seconds are
excluded only from steady cadence statistics; all events are retained.

| Biome | Relative cap (swaps/s) | Absolute cap (swaps/s) |
| --- | ---: | ---: |
| Forest | 29.8441–29.8619 | 30.0003–30.0005 |
| Coast | 29.8575–29.8730 | 29.9998–30.0006 |

All four absolute-deadline runs passed the unchanged workload controls. One old
coast run had a 13.49 ms collector cycle, above the 10 ms limit; it remains
marked as failed. Late individual swap intervals are retained in both versions.
This is a cadence fix, **not a claim that GPU rendering became faster or that
physical scanout cadence, all frame deadlines, or Phase1 cost acceptance passed**.
The earlier uninstrumented screenshots and standalone traces likewise establish
visual operation and completed swaps, not physical presentation.
