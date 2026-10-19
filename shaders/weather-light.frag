#version 300 es
precision highp float;
in vec2 uv;out vec4 solarTransmission;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "weather_common.glsl"
uniform float uWeatherCacheSlice;
void main(){
 vec2 horizontal=(uv-.5)*32.;
 float height=uWeatherLayer.x+uWeatherCacheSlice*uWeatherLayer.y;
 vec3 p=vec3(horizontal.x,height-uObserverHeight-dot(horizontal,horizontal)/(2.*Rg),horizontal.y);
 float stepLength=clamp(uWeatherLayer.y*.25,.06,1.);
 vec2 sunlight=vec2(1.);
 if(uSun.y>-.08&&uEclipse>0.){
  float opticalDepth=stepLength*(.9*weatherDensity(p+uSun*(stepLength*.5),uObserverHeight,0.)+
               1.4*weatherDensity(p+uSun*(stepLength*1.5),uObserverHeight,0.));
  sunlight=exp(-opticalDepth*vec2(1.,.2));
 }
 // Filter energy rather than optical depth; exp(-mean(tau)) would darken
 // mixed cloud/clear voxels at the cache resolution.
 float rain=0.;
 // Pack a high-resolution 504x504 rain field into B of the first sixteen
 // existing slices. Each 128-square tile has a one-texel shared border;
 // linear filtering stays continuous at tile boundaries. Solar RG is intact.
 float slice=floor(uWeatherCacheSlice*32.);
 if(uWeatherRain>.02&&slice<16.){
  vec2 tile=vec2(mod(slice,4.),floor(slice/4.));
  vec2 horizontal=((uv*128.-1.+tile*126.)/504.-.5)*64.;
  float height=uWeatherLayer.x+uWeatherLayer.y*.38;
  vec3 point=vec3(horizontal.x,height-uObserverHeight-dot(horizontal,horizontal)/(2.*Rg),horizontal.y);
  rain=weatherDensity(point,uObserverHeight,2.);
 }
 solarTransmission=vec4(sunlight,rain,0.);
}
