#version 300 es
precision highp float;
in vec2 uv;out float rainDensity;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "weather_common.glsl"
void main(){
 vec2 horizontal=(uv-.5)*64.;
 float height=uWeatherLayer.x+uWeatherLayer.y*.38;
 vec3 point=vec3(horizontal.x,height-uObserverHeight-dot(horizontal,horizontal)/(2.*Rg),horizontal.y);
 rainDensity=weatherDensity(point,uObserverHeight,2.);
}
