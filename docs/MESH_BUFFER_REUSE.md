# Reuse terrain CPU buffers at publication

A private fullscreen trace isolated the recurring terrain handoff spike.
Comparing index topology took 1.3–1.4 ms. Assigning the replacement mesh took
10–11 ms because move assignment freed the old mesh's large CPU vectors on the
rendering thread. GPU setup at those same publications took under 0.1 ms.
The total handoffs were 12.03 and 11.34 ms.

The renderer now exchanges ownership without freeing those vectors. A single
retired mesh moves into the next background build, which clears and refills its
existing storage. Current, pending/staged and retired ownership are exclusive:
the retired mesh is consumed before another replacement exists. Normal flight
continues to use two CPU meshes and two existing GPU vertex buffers, with the
same 2 MiB upload limit. Manual large debug seeks reuse either the completed
worker result or the partially staged mesh. Shutdown can release the buffers.

The same private instrumentation measured replacement handoffs at 1.43 and
1.37 ms afterwards; exchanging ownership took 0.003–0.004 ms. Raw logs and all
standalone preview events are in [mesh-buffer-reuse](mesh-buffer-reuse/).
These short diagnostic traces isolate the operation; they are not a controlled
all-weather cost, physical scanout or overall performance acceptance trial.

CPU tests poison old geometry, indices, location, flags and bounds, then verify
that a reused build matches fresh output bit for bit and retains all four CPU
buffer addresses. GPU tests exercise a second publication and debug seeks
interrupting both a running worker and a partially staged upload, with and
without volumetric weather. On AMD the recycled and fresh landscape comparisons
measured zero differences in all those fixtures. The unchanged terrain detail,
materials, lighting, culling and GPU resources are retained.

The full native build and 60/60 CPU/CLI checks passed. Expanded streaming and
seek checks also passed on Mesa/llvmpipe with two software-renderer threads;
all recycled/fresh comparisons there measured zero differences. A preceding
one-thread software run exhausted the existing 20-second functional stream
guard and remains recorded as failed. No timeout or pixel threshold was changed.
AMD water-preparation, fidelity and 41 compute/fragment comparisons passed too.

## Follow-up: retain build scratch and shorten work batches

The renderer also retains one exclusively owned normal-position workspace
across builds, avoiding allocation and release of about 12 MiB of temporary
positions for each replacement. The workspace is never shared by simultaneous
builders: a forced seek waits for the existing worker, and destruction waits
before releasing the workspace. The maximum number of meshes and GPU resources
does not change; the scratch allocation now remains resident between builds.

Vertex generation exposes a scheduling checkpoint every 64 columns; index
generation checks after each bounded block; normal passes check every 1,024
triangles or vertices. The worker retains its existing 1 ms work / 4 ms sleep
schedule. More frequent checks limit batch overshoot without changing terrain
sampling or doing catch-up work after a delayed sleep.

All 60 CPU/CLI tests pass. New tests poison the retained positions, rebuild,
and require byte-identical meshes and the same scratch storage address. The AMD
streaming/seek test passes with its original thresholds and upload limits.
Its largest recycled/fresh mean difference is 0.006632 codes (out of 255),
with the other five comparisons exactly equal. These asynchronous renderer
comparisons include shadow-cache timing; CPU terrain is compared exactly.
Mesa/llvmpipe with two threads also passed the unchanged streaming and seek
guards; its largest recycled/fresh mean difference was 0.006598 codes, with
the remaining five comparisons exactly equal.
