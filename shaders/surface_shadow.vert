#version 300 es
precision highp float;
layout(location=0) in vec3 position;
uniform mat4 uShadowMatrix;
void main(){gl_Position=uShadowMatrix*vec4(position,1.);}
