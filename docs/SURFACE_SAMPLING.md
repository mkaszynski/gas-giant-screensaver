# Periodic terrain sampling

Vertex sampling accounts for about 90% of the CPU mesh builder in a phase
breakdown. Periodic relief sampling previously used floating remainders and
repeated integer divisions to wrap every interpolation corner.

The new helper floors each coordinate once. Normal geographic coordinates fit
in `int`: power-of-two tables use an unsigned mask; other table sizes retain
integer modulo. Large finite coordinates use the original floating remainder
before conversion. Interpolation fractions and table entries are unchanged.
The world-grid interpolation also uses its existing bounded corner range to
wrap with conditional adds/subtracts instead of two divisions per tap.

Unit tests compare 220,000 random coordinates against the original calculation,
including power-of-two and other table sizes, negative coordinates, exact seams,
integer-conversion boundaries and huge finite inputs. Neighboring representable
values and signed zero are included. Fractions must match bit for bit.

Four alternating before/after CPU runs build and recycle five meshes each.
Every run serializes the same 241,936,255 bytes: all vertices, normals, material
coordinates, terrain/water indices, bounds, centers and flags. The mesh SHA-256
is `797cd764be22058109837405a8d1e78edd5ee60ae775b1dbbc688742a1825b60`.

| Route seconds | Before median ms | After median ms |
| --- | ---: | ---: |
| 0.0 | 322.212 | 277.510 |
| 5600.39 | 295.948 | 249.621 |
| 162000.0 | 228.950 | 202.351 |
| 210600.0 | 238.214 | 211.534 |
| 259199.0 | 293.692 | 252.029 |

[Raw CPU construction timings and hashes](frame-drop-investigation/periodic-sampling-paired.json)
exclude export and hashing. Both builders use C++20, Release `-O3 -DNDEBUG` and
the same Nix compiler environment; the candidate links the production library.
This is an 11–16% CPU build-time reduction, not proof of GPU frame deadlines.
Mesh resolution, sampling positions, output data and the streaming schedule are
unchanged. The header is also included in the packaged native Hyprlock source.

Reducing worker bursts to 250 microseconds was separately rejected: builds fell
behind flight and triggered multi-second synchronous fallbacks. The original
1 ms/4 ms worker schedule and 2 MiB upload chunks remain in the candidate.
