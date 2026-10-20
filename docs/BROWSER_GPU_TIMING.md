# Browser cloud timing on AMD

The native 30 fps samples in [the landscape validation](BENCHMARK_CPU_BUDGET.md)
do not establish browser performance. The targeted hardware browser check at
`37dbbfb` remains below 30 fps. It used the exact CI WASM artifact, High quality,
1920×1080 scene and display, all default effects, and the daylight forest scene:
`time=510&travel=162000&weather=1790900000&speed=1`.

After 60 completed warmup frames, 300 completed frames measured **20.76 fps**.
CPU encoding median/p95 were **11.13/16.14 ms**; p95 completion latency was
101.38 ms. Completion latency includes queuing and polling and is not GPU time.
The unchanged parent envelope was 90% of one CPU and 1100 MiB RAM. CPU quota
throttling occurred in 35 of 144 measured periods, totaling 2.41 seconds across
throttled tasks. This was a diagnostic, not a controlled native/browser
comparison or a measurement of physical screen presentation.

A separate run used GPU pass timestamps over 24 frames:

| Pass group | Mean GPU time |
| --- | ---: |
| Final weather, including clear rows | 22.64 ms |
| Background, celestial bodies and emissions | 10.57 ms |
| Land | 6.50 ms |
| Coarse cloud cache | 2.63 ms |
| Post composite | 1.41 ms |
| Solar cloud-light cache | 0.83 ms |
| Presentation | 0.53 ms |

The instrumented run totals 45.99 ms of pass time per frame. Its timestamps
must not be combined with the separate uninstrumented completion-fps sample.
Splitting the final weather draws in another diagnostic localized most of its
cost to the full solver, rather than the proven-empty-row specialization.

Chromium ran headless on the AMD adapter, with desktop environment variables
unset and no software-adapter substitution. In addition to the normal Vulkan
flags it required `--enable-gpu --disable-vulkan-surface`; the first attempt
without those flags obtained no adapter and yielded no valid timing.
[Chromium documents headless hardware GPU selection](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/docs/gpu/using-gpu-hardware-in-headless-chrome.md).
The page screenshot was blank under this headless presentation setup; direct
readback of the rendered GPU canvas confirmed the actual scene. No driver
clock or power settings were changed.

The [numeric summary and compressed completed-frame trace](frame-drop-investigation/browser-37/summary.json)
retain adapter, artifact hash, dimensions, quality, resource caps and throttling.
The only recorded console error was an HTTP 404; no GPU validation errors were recorded. A separate repeat with a static fullscreen cover over desktop applications
measured 21.28 fps under the same limits. Covering the desktop did not resolve
the browser bottleneck. Performance work remains open;
these results do not justify merging or updating the NixOS pin.
