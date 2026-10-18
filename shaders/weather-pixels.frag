#version 300 es
precision highp float;
in vec2 uv;
out vec4 color;
// Render one row and one column with the full display viewport. These tiny
// floating-point strips retain the rasterizer's exact interpolation rounding.
void main(){color=vec4(uv,0.,1.);}
