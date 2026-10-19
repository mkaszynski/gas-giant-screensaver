# Exact empty-volume weather dispatch

The native compute renderer uses a smaller specialization of the weather shader
for complete rows that provably cannot enter the lower cloud volume. It keeps
the same full-resolution output images, cirrus, fog, precipitation, optical
effects and mixed terrain/sky edge columns. All other pixels retain the full
volume solver. There is no interpolation, reduced sampling or extra cache.

Two cases permit the smaller shader:

- Above the cloud top, every ray in the row points outward from the top sphere.
- Below the cloud base, every ray in the row exits the inner cloud sphere beyond
  the solver's maximum endpoint of 100 km. This includes downward-looking coast
  pixels which previously retained the larger program despite an empty volume.

For the second case, the sphere equation at 100 km bounds the maximum allowed
normalized vertical direction. The CPU bounds ray numerators across the full
horizontal view and bounds their norms using the camera basis's Gram matrix.
Absolute cross terms keep the bound conservative for rolled and nonorthogonal
bases too. A vertical margin of .001, an altitude margin of two metres and two
pixel rows cover float conversion and interpolation error. Cuts use complete
four-row workgroups; tiny/odd final groups retain the existing bounds checks.

The two altitude cases are disjoint, so at most two dispatches cover the output
without overlap or gaps. Unsupported or nonfinite bounds keep the full solver.
Failure to compile the optional smaller shader also retains the full solver.
Browser and fragment paths continue to provide the complete reference.

Unit checks exercise pitches, rolls, nonorthogonal bases, altitude boundaries,
tiny/odd viewports and float inner-sphere intersections. The AMD compute test
exercises both dispatch cases at 1080p and compares 45 images against the full
fragment solver: zero differing display codes. Correctness does not establish
a timing improvement; whole-renderer frame deadlines remain a separate gate.

The proposed shared terrain-topology cache was removed after its controlled
comparison did not improve whole-app frame times. Its tested implementation is
preserved on `experiment/shared-terrain-topology`; the
[rejected timing report](frame-drop-investigation/rejected-topology-timing.json)
and eight compressed raw frame traces retain the evidence. It is not part of
the final implementation.
