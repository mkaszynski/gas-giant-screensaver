#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "sphere_shadow.glsl"
#include "ring_profile.glsl"
#include "ring_layer.glsl"
void main(){
 color=ringLayer(ray(),256.);
 if(color.a<=0.)discard;
}
