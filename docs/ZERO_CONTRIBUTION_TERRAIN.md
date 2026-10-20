# Zero-contribution terrain work

Terrain sampling skips computations only when their exact contribution is zero:

- Base terrain at or below −250 m has zero measured-relief and microdetail weights. Keep its base height and zero relief displacement; retain material coordinates.
- A zero continental blend weight needs no continental relief lookup. Nonzero weights, including tiny negative rounding residues, retain the original calculation.
- A zero forest weight needs no canopy-height noise. Preserve its signed zero.

Boundary tests use synthetic constant worlds at −251, −250 and −249 m, and deliberately different relief layers to verify coastal blending. Complete meshes across five routes have the same 241,936,255-byte SHA-256 as the original calculation: `797cd764be22058109837405a8d1e78edd5ee60ae775b1dbbc688742a1825b60`.

Four alternating before/after CPU construction runs use the same Release compiler and assets; the helpers link their corresponding production libraries. Timings exclude serialization and hashing. No GPU workload runs concurrently.

| Route seconds | Before median ms | After median ms |
| --- | ---: | ---: |
| 0 | 284.321 | 151.694 |
| 5600.390625 | 248.597 | 179.704 |
| 162000 | 200.353 | 202.007 |
| 210600 | 210.399 | 191.095 |
| 259199 | 248.891 | 119.014 |

[Raw paired construction timings and output hashes](frame-drop-investigation/zero-contribution-paired.json).
These measurements establish CPU work saved, not GPU frame deadlines. Geometry, normals, materials, resolution and lighting are unchanged.
