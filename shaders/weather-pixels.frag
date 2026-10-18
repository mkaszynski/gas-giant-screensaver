#version 300 es
precision highp float;
in vec2 uv;
out vec4 color;
uniform vec2 uWeatherCacheSize;
// Capture the full viewport once on creation/resize. Interpolation can vary
// across rows by a few float bits, so separable strips are insufficient.
// Keep the cache-cell arithmetic in the fragment stage as well: drivers can
// contract its multiply/add differently when compiling a compute program.
void main(){color=vec4(uv,uv*uWeatherCacheSize-.5);}
