# Atmospheric optics

The airborne weather renderer includes 22-degree ice halos, sundogs, liquid-cloud
glories, rainbows and fogbows. These are angular scattering features of the
existing cloud and precipitation volumes, rather than screen-space decals.
Their visibility depends on the Sun, weather, cloud illumination and the view.

* Thin high ice clouds produce halos. A minority of horizontally oriented plates
  adds sundogs near the Sun's elevation; their separation increases as the Sun
  rises. The halo has a red inner edge and a softer blue outer edge.
* Warm liquid cloud tops can show a small glory around the antisolar direction,
  usually below the aircraft. The colored rings require a sunlit cloud bank in
  the appropriate direction.
* Sunlit rain produces a primary rainbow opposite the Sun. Small fog droplets
  produce a broader, less saturated fogbow. An overcast storm does not guarantee
  a bow: the droplets must receive direct sunlight.

Solar optics vanish during total eclipse and deep night. They inherit the
weather renderer's terrain distance, cloud extinction, atmosphere transmission,
partial-coverage composition and HDR exposure. There is no always-visible halo
or artificially luminous night rainbow. Mixed-phase cloud and fog fractions
use regional temperature and a standard lapse-rate estimate, transitioning
continuously between -20 and -2 degrees Celsius. High cirrus uses ice crystals.

## Model and cost

The committed `assets/world/optics/phase.ggoptics` is a 4096-by-67 RGBA16F
lookup table (2,195,472 bytes including its header). It is loaded once into a
single immutable texture. The runtime does not trace crystals, sum Mie series,
generate noise or load photographs for these effects. Native OpenGL, browser
WebGL 2 and WebGPU use the same shader functions and table.

Ice side-face refraction uses 60-degree hexagonal prisms, dispersive ice indices
and unpolarized Fresnel transmission. Continuous inverse-Snell bin integration
and orientation quadrature avoid histogram caustics. The prescribed mixture
retains 85% of the original broad cirrus scattering, assigning 12% to randomly
oriented prism paths and 3% to horizontal plates with a 0.8-degree normal wobble.
Plate rows retain their declining transmitted power at high solar elevation.

Liquid scattering uses Lorenz-Mie calculations averaged over lognormal radius
distributions: cloud 10 micrometres (log sigma .08), fog 4 micrometres (.15),
and rain 250 micrometres (.15). Twenty-five visible wavelengths, a 5778 K
illuminant, CIE color matching and the finite 0.533-degree solar disk are
integrated offline. The runtime multiplies RGB phase values by its existing
atmospheric solar transmission. Liquid phase functions replace the broad direct
scattering term, while ambient illumination is retained.

No framebuffer, blur pass or additional cloud density samples are added.
Cloud phase is evaluated once per view ray outside the volume march. Cirrus
uses filtered angular lookups; precipitation retains four density samples,
bounded to the rain layer so nearby curtains cannot fall between distant taps.
The parent-cloud rain field is evaluated once in additional channels of the
existing cloud-light pass. Sixteen overlapping tiles in that texture's depth
slices provide a continuous 504-square field over 64 km; beyond it the shader
uses direct sampling. This adds 2 MiB to the existing light-cache allocation
and avoids repeating cloud-density calculations at every precipitation sample.
The solar-transmission channels retain their original grid and values.
Existing terrain detail, resolution, cloud quadrature and effects remain intact.
Actual full-scene timing must be measured on the target GPU; texture size alone
does not establish a frame-rate guarantee.

This is a single-scattering visual model within the existing approximate weather
lighting system. Droplet populations and crystal mixtures are prescribed, not
predicted microphysics. It does not solve polarization or full spectral multiple
scattering. The coarse weather cache can soften small liquid-cloud features;
ice halos are evaluated in the full-resolution weather composition.

## Review and reproduction

The effects are enabled by default with airborne weather. Existing debug-menu
weather presets help review different atmospheric states. An effect appears
only when its physical geometry and illumination are present; Fair is not a
command to draw a complete halo. `--no-atmospheric-optics` disables the additional
phase functions for comparison. The browser equivalent is `?optics=0`.

`./build/optics-render-tests captures` renders isolated illuminated layers using
the production phase and composition functions, writing halo, glory and rainbow
PNGs. These controlled fixtures are optical checks, not ordinary weather scenes.
The tests also check eclipse, darkness, clear-air and opaque-ground behavior.

Rebake the committed table in an offline Python environment:

```sh
python3 -m venv /tmp/observatory-optics
/tmp/observatory-optics/bin/pip install -r scripts/optics-requirements.txt
MIEPYTHON_USE_JIT=1 NUMBA_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
  /tmp/observatory-optics/bin/python scripts/bake_atmospheric_optics.py
/tmp/observatory-optics/bin/python tests/test_atmospheric_optics.py
```

The manifest records the binary checksum, dimensions, physical parameters,
source references and generator version. Scientific Python dependencies are
offline development tools; installation and screensaver startup need none.

## Sources

* [NOAA: halos, sundogs and pillars](https://www.weather.gov/arx/why_halos_sundogs_pillars).
* [Warren and Brandt: ice optical constants](https://atmos.uw.edu/ice_optical_constants/).
* [Daimon and Masumura: water refractive index](https://doi.org/10.1364/AO.46.003811).
* [MiePython normalized unpolarized scattering](https://miepython.readthedocs.io/en/latest/api/miepython.core.i_unpolarized.html).
* [NASA: glories over cloud tops](https://earthobservatory.nasa.gov/images/8805/glory-over-pacific-ocean-clouds).
* [USGS: rainbows, water and light](https://www.usgs.gov/water-science-school/science/rainbows-water-and-light).
* [CIE matching-function approximation](https://jcgt.org/published/0002/02/01/).
