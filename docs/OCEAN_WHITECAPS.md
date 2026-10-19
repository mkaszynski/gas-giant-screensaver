# Weather-driven ocean whitecaps

The airborne ocean uses wind-dependent whitecaps in its existing water shading
pass. Prescribed 10-m wind follows the continuously changing cloud/front state:
fair conditions favor sparse whitecaps, storms increase them, and quiet fog
suppresses them. This is a sea-state estimate, not a numerical wind forecast.
Frozen water and land retain their existing materials.

Coverage follows the two continuous wind fits measured by Callaghan et al.
(2008), converted from percent to fractional area and clamped to the observed
3.70–23.09 m/s range. Low winds have zero breaking coverage. The offshore model
does not add surf-zone breakers, spray particles, boats or shoreline decals.

A three-wave crest score uses the existing moon-fixed water coordinate basis
and prescribed 192/310/470 m breaking-wave groups with real-time deep-water
dispersion. Shorter wind waves continue supplying the existing reflection normals. Waves form, move and lose foam continuously;
they follow the time slider and reconstruct deterministically after a seek.
A 64-entry inverse-CDF table maps the requested area fraction to the crest
threshold. The table is generated offline by angular quadrature and interpolated
on CPU, rather than integrating distributions during rendering.

Phase footprints filter the narrow crests. Once they become unresolved, their
area-average coverage replaces isolated bright pixels. There is no frame noise,
photographic texture, additional texture residency, render target or draw pass.
The representation is a statistical crest model, not a breaking-wave fluid solver
or persistent bubble-advection simulation.

Foam replaces the covered water's reflection and glint; it is not simply added
on top. Visible age-averaged diffuse reflectance is approximately 0.22, with a
slightly lower red channel for water absorption. Sun, atmosphere, cloud shadows,
skylight and gas-giant light determine its color and brightness. It has no
self-emission. The model omits spectral bubble scattering and full multiple
scattering inside foam.

`--no-whitecaps` disables only this effect for comparison. The browser equivalent
is `?whitecaps=0`. Existing debug weather presets exercise clear, fair, overcast,
fog and storm sea states. A storm can hide the water beneath clouds, which is
physical occlusion rather than a missing effect.

Rebuild the small committed quantile header with the existing offline NumPy
environment:

```sh
python3 scripts/bake_ocean_quantiles.py
```

`ocean-foam-tests` checks coverage units, continuity, empirical bounds, prescribed
sea states and independently sampled crest areas. `whitecap-render-tests` isolates
sunlit storm wind from procedural cloud placement and checks visible foam, calm
water, dark conditions, subframe stability and deterministic seeking. Its optional
output-directory argument writes comparison PNGs. These fixtures are not ordinary
weather scenes. Full-scene timing and native previews must also be checked before
release; a small model alone does not establish a 30 fps guarantee.

## Sources

* [Callaghan et al. 2008: wind speed and whitecap area](https://doi.org/10.1029/2008GL036165).
* [NASA/PACE: visible age-averaged foam reflectance and spectral absorption](https://ntrs.nasa.gov/api/citations/20210011628/downloads/feart-07-00145.pdf).
* [Callaghan et al. 2024: evolving foam on real breaking waves](https://doi.org/10.1029/2023JC020193).
