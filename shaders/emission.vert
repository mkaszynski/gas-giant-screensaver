#version 300 es
precision highp float;
layout(location=0) in vec2 aScreen;
layout(location=1) in vec3 aPoint;
layout(location=2) in vec3 aRadiance;
layout(location=3) in vec4 aProfile;
out vec3 point,radiance;
out vec4 profile;
void main(){gl_Position=vec4(aScreen,0.,1.);point=aPoint;radiance=aRadiance;profile=aProfile;}
