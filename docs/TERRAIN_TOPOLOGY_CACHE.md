# Shared terrain connectivity

Terrain flight updates change vertex positions, normals, materials and bounds;
they do not change the polar lattice's triangle connectivity. Previously each
background update rebuilt both index arrays, and publishing the mesh compared
the complete terrain index array on the rendering thread.

`buildSurfaceMesh` now shares one immutable `SurfaceTopology` per process. Its
block layout also stores the invariant draw offsets and counts. Each update
still computes the complete original vertices, normals and conservative bounds.
The renderer compares topology identity when deciding whether to upload its
element buffers. Initial uploads and the existing reflection indices are
unchanged. Mutable vertex/bounds storage continues to be recycled exclusively
between the rendering thread and terrain worker.

The cache is initialized once using C++'s thread-safe local static initialization;
only const shared ownership escapes initialization. It has a fixed size, with
no location-dependent entries or eviction. GPU buffers, mesh resolution, water
LOD, material detail, shadows and weather settings are unchanged.

The [independent geometry comparison](frame-drop-investigation/terrain-topology-parity.json)
serialized 241,936,255 bytes from the original and cached builders over five
flight locations, including land, coast and ocean. All bytes matched. Unit tests
check shared identity across locations and cooperative builds, reusable storage,
restoration of canonical connectivity after caller-owned data is replaced,
valid indices, conservative bounds and watertight fine/coarse water boundaries.

This removes known redundant work; it does not by itself establish a sustained
30 fps result. Controlled whole-renderer timings are required separately.
