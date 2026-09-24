# Lightning and visible auroras

These are visible-light approximations for the fictional warm H2/He giant,
not UV/infrared imagery recolored into visible light. The upper atmosphere is
water-cloud dominated by assumption. Stellar distance alone does not determine
cloud opacity, storm activity, magnetic field, or magnetospheric plasma supply.

## Lightning

Twenty-four persistent storm regions at 18–61 degrees latitude rotate with
the cloud texture. Each has one jittered event per 7–16 physical seconds. Events
contain one to three optical pulses separated by 47 ms, with 12–20 ms decay
times and a finite cutoff. The same time compression used for the orbit applies to both event rates and
pulse durations: about 17.8× at a 30-minute day. Their integrated physical energy
is identical at 15, 30 and 60 fps, including accelerated pulses shorter than a
frame. Exposure integration divides that energy by the physical shutter length;
it does not artificially brighten sub-frame flashes. Hyprlock uses its existing
scene clock and unlock slowdown. Paused rendering does not replay a backlog.

The model uses 10^7–10^9.5 J of upward escaping visible energy, weighted toward
weaker events. This is an assumed distribution within Jovian optical estimates,
not a measured distribution for a habitable-zone giant. Thirty-five percent of
events have compact 35–70 km FWHM cloud footprints; the rest span 90–160 km.
The compact population represents flashes with less overlying water-cloud
screening. Water clouds do not supply an arbitrary brightness multiplier.

Cloud glows use a normalized Gaussian footprint and approximately white light,
with no resolved branching bolts, persistent afterglow, or fullscreen bloom.
Upward Lambertian power is converted to radiance using the footprint area and
535 W/m² as approximate visible solar irradiance. Oblique projected flux is
reduced by the viewing cosine. The unresolved footprint is screen-circular;
resolving a foreshortened three-dimensional cloud is outside this approximation.

## Aurora

A pair of persistent irregular magnetic ovals corotates with the giant. The
assumed northern/southern magnetic offsets are 10/3 degrees and the oval is
about 20 degrees from its magnetic pole, with modest longitude variation.
Its 350 km FWHM width follows the scale of Galileo's visible arcs. A nominal
120 km height above the cloud deck and 35 km emitting-layer thickness are
assumptions for the warmer, higher-gravity planet, rather than copied Jovian
altitudes relative to the 1-bar level.

Normal-view broadband brightness varies slowly around 160 kR, within the
80–300 kR scale measured by Galileo. Photon radiance is converted with a
representative 650 nm photon energy. The restrained reddish RGB mixture is an
approximation to hydrogen lines plus continuum, not a measured spectrum of
this fictional planet. Finite layer thickness limits limb brightening. The
main ovals remain active in sunlight but are overwhelmed by reflected light.
Neither eclipses nor ring shadows turn electrical emission off.

No bright UV dawn-storm power is added to RGB. Moon footprints require an
assumed plasma interaction model and are not synthesized for all 20 moons.
The low-latitude, nearby observer makes these visible-light ovals very faint;
much of an oval can lie behind the planet. At the ordinary display exposure
they can quantize to zero. Favorable eclipse views retain a faint arc, not a
prominent colorful curtain. Shader-level nonzero radiance alone is not evidence
of perceptible visibility.

## Rendering and review

Both effects now receive an explicit **5× visible-radiance boost** at rendering,
as requested for visibility. The physical baseline below the gain, event rate,
pulse duration, geometry, filtering and occlusion are unchanged. This is an
artistic brightness adjustment, not a revised Jovian measurement. Tone mapping
means final display-code values do not increase by exactly five.

One additive geometry batch contains only the active flash patches and two
256-segment auroral strips. No extra framebuffer, shadow map, fullscreen blur,
or texture is allocated. Gaussian pixel integration preserves unresolved flash
energy. Auroral strips filter both their width and segment endpoints. Foreground
moons and the planet obscure emissions; rings attenuate them by optical depth.
The moon's atmosphere, clouds and mountains are applied through the existing
compositor after/before the appropriate stages.

Fixed 128× internal pre-exposure preserves dim emission in the existing RGBA16F
scene buffer, with explicit high-precision sampling when decoding it. Very high
values are bounded before half-float overflow; the solar disk remains far above
display white. A bounded dark-scene exposure now responds to solar visibility
and the giant's Lambert phase, increasing sensitivity only with a dim sky and
dim giant (up to eight stops). This is a deterministic display approximation,
not a retinal-adaptation simulation; it does not change emitted energy. Bright
rings can overexpose as they would in a longer photographic exposure. Decorative
sky/ground light floors and artist-scaled stars retain their display brightness
instead of being amplified along with physical emission. GPU tests compare actual
linear radiance with 8×-per-axis references and fractional-pixel camera motion.

`--weather-time SECONDS` sets the starting weather clock for repeatable previews.
It defaults to the starting scene time. Lightning converts this display-clock
time to physical time using the orbital compression factor; aurora variation
uses display-clock time directly. `--no-emissions` disables both effects for comparisons. Screenshots
freeze both clocks. Recording advances each clock by `--time-step`; use
`--time-step 0.033333333 --fps 30` for real-time 30 fps sequences.

## Observational sources

- [Little et al. (1999), Galileo Images of Lightning on Jupiter](https://doi.org/10.1006/icar.1999.6195): cloud footprints and optical energies.
- [Becker et al. (2020), Small lightning flashes from shallow electrical storms on Jupiter](https://doi.org/10.1038/s41586-020-2532-1): smaller flashes, millisecond timing and multiple pulses.
- [Wong et al. (2026), Radio Pulse Power Distribution](https://doi.org/10.1029/2025AV002083): uncertain intrinsic optical distribution and cloud screening; radio power is not an RGB brightness calibration.
- [Ingersoll et al. (1998), Imaging Jupiter's Aurora at Visible Wavelengths](https://doi.org/10.1006/icar.1998.5971): approximately 80–300 kR overhead broadband radiance.
- [Vasavada et al. (1999), Jupiter's visible aurora and Io footprint](https://doi.org/10.1029/1999JE001055): persistent thin arcs, altitude and optical power.
- [NASA, Night Side Jovian Aurora](https://science.nasa.gov/photojournal/night-side-jovian-aurora/): visible emission, display-color caveat and hydrogen interpretation.

## Reproducible visibility check

Restart an already-running preview after rebuilding; it retains its previous
executable and compiled shaders. An eclipse provides a useful lightning check:

```sh
./build/gas-giant-screensaver --fullscreen --time 430350
```

Before the 5× rendering boost, at 1080p, scene 430350 / weather 430354.833333333 changes the brightest lightning
pixel by 49/255 in the development GPU's final output. The previous exposure
produced only 5/255. A favorable aurora at scene 437625 / weather 103.04 changes
a channel by only 4/255, so it may still be imperceptible on a monitor. These
are output-code differences, not measurements of human visual thresholds.
Sunlit scenes continue to wash the effects out.
