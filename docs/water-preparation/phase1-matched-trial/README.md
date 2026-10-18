# Matched original landscape budget: passed

All 24 predeclared runs completed with the immutable water-preparation/celestial-
bound package. The baseline remains original Phase1 commit
`f882d4d701308b06c25be0a2118871dc244f9794`, binary SHA-256
`776dde86a09f15a10a9e02a64f1b08d5e14e7592c97d6aafb9eec655e5a02865`.
The candidate binary SHA-256 is
`0fc2ab37af78f7035bfb2117bf389cbf1f5f0ca8c0ae57a18bd6063b4b438378`.
Its shaders match the product code through `68b5a4c`; the later preview-pacing
change is separate and is not certified by this older-artifact cost trial.

The candidate uses `--no-weather` solely to match original Phase1's thin-cloud
landscape scope. Default volumetric weather remains enabled in the product and
has a [separate full-weather trial](../fullscreen-condition-trial/).
Both use 1920×1080, a 30 fps cap, identical scene/route/weather seeds, and three
alternating baseline/candidate pairs per biome. All original cost, deadline and
workload gates are unchanged. A static fullscreen cover hides other windows
without stopping their applications or changing GPU clocks/power settings.

| Biome | Median candidate / baseline | p95 candidate / baseline |
| --- | ---: | ---: |
| Ocean | 0.8923 | 0.6172 |
| Forest | 0.9958 | 0.7963 |
| Snow | 0.9225 | 0.7990 |
| Coast | 1.0653 | 0.7823 |

Every individual pair and every aggregate stayed below the 1.30 cost limit.
All 24 workload-control checks and all 12 pair-matching checks passed. All
2,040 steady candidate frames met 33.333 ms, excluding only the ten predeclared
startup frames per run from deadline counts. Complete-renderer cost summaries
include startup, as before. Every candidate frame CSV, both versions' logs and
all workload telemetry are retained; gzip files use mtime zero.

An earlier preflight on the same package had stopped before any rendering at
1.25 background CPU cores. This trial started after the operator paused other
work and signaled readiness again; it is not an automatic repeated render trial.
The preceding full-weather trial's late frames remain failed, and physical
presentation remains separate. This pass certifies this package's original
landscape cost comparison, not overall release or all-weather acceptance.
