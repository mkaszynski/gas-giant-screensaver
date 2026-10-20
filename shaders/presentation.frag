#version 300 es
precision highp float;
in vec2 uv;
out vec4 color;
uniform highp sampler2D uPresentation;
void main(){color=texture(uPresentation,uv);}
