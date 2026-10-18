# Conservative celestial draw bounds

Planets and moons now use the exact perspective silhouette of their bounding
sphere, including the giant's existing 12-scale-height atmosphere. Rings use
the projected outer circular disk. Both retain an eight-pixel filtering guard;
shader code, samples, textures, lighting, shadowing and transparency are unchanged.
This avoids invoking expensive shaders over pixels outside their support.

Near-camera-plane geometry falls back to unrestricted drawing. Nearly edge-on
or tiny ring ellipses do too: the existing implicit-conic derivative filter can
extend beyond the geometric ellipse. The initial implementation failed its
software image comparison by six display codes in a nearly edge-on fixture;
the conservative fallback addresses that case without changing the filter.
It uses a lower bound on the projected minor semiaxis, derived from the
ellipse covariance determinant and trace, rather than a fixed orbital angle.

`projection-tests` checks independent sampled sphere and disk points across
800 scaled, offset, near-plane and differently oriented configurations.
`projection-render-tests` compares the complete bounded image with unrestricted
celestial drawing. It covers 31 fixtures, including day/night/eclipses,
overlapping moons/rings, edge-on and face-on rings, off-center/near/distant
views, odd dimensions, and four complete 1920×1080 biome/weather scenes.
Host scissor-state restoration is checked for every draw.

The final 31 AMD comparisons and 31 Mesa/llvmpipe comparisons had zero differing
RGB display codes. This establishes image parity for those fixtures. It does
not establish sustained 30 fps, original Phase 1 cost acceptance, or physical
presentation cadence. Those performance requirements remain open.

Run the focused checks from the source root after building:

```sh
./build/projection-tests
./build/projection-render-tests
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 ./build/projection-render-tests
```
