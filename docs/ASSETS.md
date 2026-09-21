# Assets and provenance

`assets/giant.png` and `assets/mountains.png` were created for this project using
OpenAI's built-in image generation tool. They are generated imagery, not NASA
observations or photographs of an identified mountain range. The project offers
these original generated assets under CC0-1.0 to the extent rights exist.

The renderer supplies all directional light, astronomical phases and eclipses.
The mountain photograph-style layer is a fixed-view approximation; its internal
surface relief does not cast changing geometric shadows.

## Mountain generation prompt

Generate a production texture asset for a photorealistic astronomical screensaver.
A wide panorama (3:1 aspect ratio, high resolution) of a distant rugged Earth
alpine mountain range, finely detailed blue-gray rock and restrained patchy snow,
many overlapping distant ridges, irregular natural silhouette. View from far
away, no close foreground objects, no trees, no buildings, no people, no sun,
no text. Mountains fill bottom 45 percent of the image, highest peaks at about
middle height, lower slopes continue beyond bottom edge. Upper area must be
genuinely transparent alpha sky, carefully cut out around fine mountain
silhouette. Flat neutral overcast diffuse illumination with no baked-in
directional shadows, no bright highlights, no strong blue atmospheric haze;
suitable for real-time relighting from dawn to night. Natural photographic
detail, not painterly, not fantasy spires. This is a standalone landscape layer,
not a finished sky scene.

## Gas giant generation prompt

Create a high resolution 2:1 equirectangular planetary ALBEDO TEXTURE MAP for a
fictional photorealistic Jupiter-like gas giant. This is a flat rectangular
cylindrical surface map, NOT a sphere, NOT a scene. Horizontal swirling
atmospheric bands in ivory, pale ochre, slate blue-gray, subtle copper and muted
rust. Rich fine fluid turbulence, tiny vortices, delicate feathering and wispy
filaments, several restrained oval storms. NASA spacecraft photography level
detail. Seamless periodic left/right edges. Flat uniform diffuse illumination
everywhere, NO shadow, NO limb shading, NO sun highlight, NO atmosphere glow,
NO stars, NO text. Bands extend horizontally across the entire rectangle,
small-scale turbulence throughout, organically irregular rather than repetitive
stripes. Balanced desaturated colors, natural scientific beauty.

## Source attribution

The standalone renderer and observatory widget are original MIT-licensed code.
The input-trace, unlock-transition and failure-halo patches originate in
[Hyperspace Starfield](https://github.com/akaszynski/hyperspace-starfield), pinned
at `6f42dae4592fbcc87811965792d15d2bfc96e4bd`. They modify BSD-3-Clause Hyprlock
code; retain `hyprlock/LICENSE-BSD-3-Clause.txt` when redistributing them.
The original project's GPL starfield renderer is not included in this project.

The atmosphere implementation follows the LUT architecture and isotropic
multiple-scattering feedback described by Sébastien Hillaire, *A Scalable and
Production Ready Sky and Atmosphere Rendering Technique* (2020), with original
GLSL code and reduced sample counts for a stationary ground observer:
https://doi.org/10.1111/cgf.14050

## Green mountain revision prompt

Edit this production panorama texture: preserve the exact mountain silhouette,
width, framing, transparent alpha sky, and fine photographic detail. Make the
mountains substantially greener and more inviting: naturally forested deep
green slopes and muted green alpine meadows, with gray rock exposed mainly near
highest peaks and only very small traces of snow. Distant temperate Earth-like
mountains. Keep flat neutral overcast illumination, no directional sunlight, no
bright highlights, no painted look, no text, no added sky. All space above the
original ridge must remain truly transparent; lower slopes continue beyond
bottom edge. It will be relit dynamically through a day/night cycle in a renderer.
