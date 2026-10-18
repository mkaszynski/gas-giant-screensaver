# Water preparation at its use

The terrain fragment shader previously prepared wave amplitudes and the water
normal before either the pure-water return or the full land material path.
Those values then remained live across the land's texture, normal and lighting
work before the final shoreline blend. The shader now prepares them at each
water call. The wave footprint remains unconditional, before any divergent
shoreline branch, preserving its derivative filtering. All existing wave,
reflection, material, atmosphere and shadow expressions remain unchanged.
No new texture, cache, sample reduction or lower resolution is involved.

`water-preparation-tests` reconstructs the previous evaluation order from the
shared expressions and compares complete rendered images. Its 28 fixtures cover
four full-HD biomes in daylight, night lightning and storms; moving coastlines
at odd dimensions; thin clouds with reflections disabled/enabled; and volumetric
clouds with terrain reflections enabled. The existing maximum-two-code/0.02-mean
RGB error gate is unchanged. All 28 AMD and all 28 Mesa/llvmpipe comparisons
actually measured zero differences. The existing water, surface, fidelity,
weather-specialization and 41 compute/fragment comparisons also passed on AMD.
CPU/CLI checks passed 60/60. Image evidence is in [water-preparation](water-preparation/).

The initial ABBA stage diagnostic measured coastal terrain at 11.29 ms before
and 8.81 ms after, with complete frames at 32.54 and 29.84 ms respectively.
Forest complete-frame medians were 28.98 and 28.78 ms. These measurements had
failed background-work controls and establish neither sustained 30 fps nor the
original Phase 1 cost requirement. Every raw frame and failed workload control
is preserved in that evidence directory. Controlled fullscreen-condition timing
is being recorded separately; release acceptance remains open.

Run the focused comparison from the source root after building:

```sh
./build/water-preparation-tests
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 ./build/water-preparation-tests
```
