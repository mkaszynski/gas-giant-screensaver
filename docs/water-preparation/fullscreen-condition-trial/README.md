# Fullscreen-condition native timing

The uncovered preflight stopped before rendering because external GPU use was
0.0653, above the unchanged 0.05 limit. A static fullscreen black buffer was
then drawn once for the entire trial. It hides the terminal and other windows
as the fullscreen screensaver normally does. Other applications keep running;
no power setting, GPU clock or product quality setting changed.

The subsequent trial completed all 16 predeclared runs: four biomes, with
previous/candidate/candidate/previous ordering, 180 frames per run. Both builds
use OpenGL compute for clouds. The old `fragment` and `compute` fixture labels
are identified explicitly in result.json and do not describe their backends.
The candidate includes conservative celestial bounds plus water preparation.
This comparison does not isolate either individual change.

| Biome | Previous median (ms) | Candidate median (ms) | Candidate steady late frames |
| --- | ---: | ---: | ---: |
| Forest | 25.08 | 23.62 | 1/340 |
| Coast | 28.29 | 24.85 | 0/340 |
| Snow | 21.54 | 20.64 | 0/340 |
| Ocean | 16.65 | 15.77 | 1/340 |

These are medians of run medians, including all startup frames in the cost
statistics. Only deadline counts exclude the ten predeclared startup frames.
Fifteen of sixteen workload-control checks passed. One ocean candidate read
cycle took 11.26 ms, exceeding the unchanged 10 ms limit. One forest frame and
one ocean frame exceeded 33.333 ms. The other 1,358 steady candidate frames met
the deadline. This is useful headroom evidence, **not overall acceptance**.

All 2,880 frames, logs and workload telemetry are retained, including the two
late frames and the failed collector control. Compressed files use gzip mtime
zero. Path substitutions remove local usernames without changing numeric data.
The original Phase1 matched-cost and physical-presentation requirements remain
separate outstanding checks.
