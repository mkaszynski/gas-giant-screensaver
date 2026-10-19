#version 300 es
precision highp float;
in vec2 uv;out vec2 solarTransmission;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "weather_common.glsl"
uniform float uWeatherCacheSlice;
void main(){
 vec2 horizontal=(uv-.5)*32.;
 float height=uWeatherLayer.x+uWeatherCacheSlice*uWeatherLayer.y;
 vec3 p=vec3(horizontal.x,height-uObserverHeight-dot(horizontal,horizontal)/(2.*Rg),horizontal.y);
 float stepLength=clamp(uWeatherLayer.y*.25,.06,1.);
 float opticalDepth=stepLength*(.9*weatherDensity(p+uSun*(stepLength*.5),uObserverHeight,0.)+
              1.4*weatherDensity(p+uSun*(stepLength*1.5),uObserverHeight,0.));
 // Filter energy rather than optical depth; exp(-mean(tau)) would darken
 // mixed cloud/clear voxels at the cache resolution.
 solarTransmission=exp(-opticalDepth*vec2(1.,.2));
}
