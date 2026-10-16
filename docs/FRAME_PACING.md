# Native frame timer drift

The native observatory previously scheduled each timer relative to the actual
start of the latest draw. Millisecond rounding and timer delivery delays became
part of every new interval. Even when rendering fit comfortably in the budget,
the resulting submission rate stayed slightly below the requested 30 fps.

`FramePacer` now retains a steady-clock deadline across callbacks, including the
fractional milliseconds. Rendering and wakeup delay consume the existing
interval rather than shifting all subsequent deadlines. A long idle period
restarts the cadence; an overrun retries after 1 ms without queuing missed
frames. Each output still has one pending timer. Scene time continues to follow
elapsed time, including after output suspension.

This changes only native scheduling. Terrain, shaders, textures, streaming,
upload limits and visual quality settings are unchanged. The standalone preview
uses its existing pacing path.

## Regression and package checks

Deterministic tests simulate ten seconds of 30/60 fps with 7 ms rendering and
750 microseconds of timer delivery delay. The old relative timer accumulated
125 ms of phase error at 30 fps; the deadline timer stays within 2 ms. Additional
tests cover overruns, a 60-second idle interval and invalid frame rates.

Local validation includes 40 CPU/CLI checks, 38 offline model/asset checks, six
native-log parser tests, all 13 GPU test programs on AMD hardware and Mesa
software, and benchmark CSV integration on both backends. Nix flake checks build
the standalone and native packages and validate the lock configuration.

The log parser accepts padded Wayland timestamps and both object delimiters,
counts surface commits rather than callback requests, and retains long
intervals. Failed runs preserve their client/compositor logs. The benchmark
cleans up its owned session, including fatal-error helpers, without addressing
the desktop lock.

## Scope of the measurements

`scripts/benchmark_native.py` creates a private compositor runtime and one owned
1920×1080 headless output at 60 Hz. It renders the packaged Hyprlock observatory,
waits five seconds, then records twenty seconds of Wayland surface submissions.
Each report retains every inter-submission interval and the immutable package
and asset paths. Baseline and candidate use the same approved Phase 2 assets.

The compositor requires the running Wayland parent as a renderer donor. Reports
record the parent's output dimensions, refresh rates and DPMS state. This is a
submission-cadence measurement, **not physical presentation or scanout FPS**.
Other desktop GPU work and automatic GPU clocks remain possible influences.

Reproduce with the native package and a standalone asset directory:

```sh
python3 scripts/benchmark_native.py --package ./result-stalls-lock --data-dir ./result-stalls/share/gas-giant-screensaver --output /tmp/native-cadence.json
```

An optional `--capture /tmp/native.png` captures only the owned output, after the
timing interval. The owned lock is subsequently dismissed by its signal path;
this does not test PAM authentication.

## Repeated packaged results

Measured candidate source: `e2982e26fbf1665bcad5c23668fc4dbedb6982a0`.
The reference is the approved Phase 2 native package at `c7adb00`.
Three pairs ran in baseline/candidate, candidate/baseline, baseline/candidate
order. The parent had one active 1920×1080 output at 60.026 Hz with DPMS on;
the desktop lock remained running. Native scene time continued normally.

| Pair | Version and raw intervals | Submissions/s | Median ms | p95 ms | Maximum ms |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | [baseline](frame-pacing/native-1-baseline.json) | 30.053373 | 32.968 | 34.519 | 50.168 |
| 1 | [candidate](frame-pacing/native-1-candidate.json) | 30.000931 | 33.202 | 34.836 | 51.600 |
| 2 | [baseline](frame-pacing/native-2-baseline.json) | 30.016456 | 33.135 | 34.501 | 51.809 |
| 2 | [candidate](frame-pacing/native-2-candidate.json) | 29.999601 | 33.221 | 35.183 | 50.636 |
| 3 | [baseline](frame-pacing/native-3-baseline.json) | 29.818940 | 33.199 | 36.133 | 51.109 |
| 3 | [candidate](frame-pacing/native-3-candidate.json) | 29.941196 | 33.225 | 36.598 | 54.379 |

Two candidate runs stayed within 0.001 submissions/s of 30; the third measured
29.9412. The reference varied from 29.8189 to 30.0534. This supports more accurate
average cadence, not improved rendering tails: p95 and maximum intervals do not
improve consistently, and candidate intervals still reach 54.379 ms. Submission
rates are not a proof of physical 30 fps presentation.

The rebuilt native package rendered a valid full-size scene on its owned output
and exited successfully through the signal-driven unlock path. No benchmark
compositor or fatal-error helper remained afterward. The standalone candidate
and approved package produced **identical RGB pixels** at 1920×1080, sky time
900 and flight time 162000, using the same frozen asset directory.
[Image comparison metadata](frame-pacing/image-comparison.json) records the
fixture and decoded RGB hashes. This is an unchanged-image check, not evidence
of additional visual quality.

## Remaining acceptance

The timer fix does not establish uninterrupted 30 fps. Long submission intervals,
synchronized renderer stalls and physical multi-output presentation remain
separate concerns in [issue 13](https://github.com/mkaszynski/gas-giant-screensaver/issues/13).
The original Phase 2 tail measurements and failed deadline acceptance remain
valid. Terrain streaming was ruled out as the sole stall cause by the fixed-view
control; GPU clock variation alone is not causal evidence of a governor fault.

Operator review is required before merging or changing the NixOS source pin.
