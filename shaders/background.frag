#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform highp samplerCube uGalaxy;
uniform mat3 uStarFrame;
uniform bool uGalaxyEnabled;
uniform float uCelestialGain;
void main(){
 vec3 d=ray();vec3 L=sky(d);vec3 T=viewT(d);
 float night=1.-smoothstep(.00002,.003,dot(L,vec3(.2126,.7152,.0722)));
 if(uGalaxyEnabled && night>0.){
   vec3 world=transpose(uStarFrame)*d;
   // Cached deterministic stellar-population/dust integration, in linear HDR.
   // Resolved catalog stars have a separate flux budget and optical profile.
   // Same magnitude zero point as the catalog. .8 is the reference point-star
   // flux scale and 4*tan(29deg)^2/1080^2 is its reference pixel solid angle.
   L+=texture(uGalaxy,world).rgb*8.429589150e-07*night*uCelestialGain/uDarkGain*T;
 }
 // Most pixels cannot intersect the solar disk. Keep the original angular
 // antialiasing/limb model inside a cheap conservative cosine bound.
 float cosine=dot(d,uSun);float aa=uTanFov/uResolution.y;
 if(uEclipse>0. && cosine>cos(.00465047+aa)){
   float angle=acos(clamp(cosine,-1.,1.));
   float disk=1.-smoothstep(.00465047-aa,.00465047+aa,angle);
   float radiusFraction=clamp(angle/.00465047,0.,1.);
   float limb=.4+.6*sqrt(max(0.,1.-radiusFraction*radiusFraction));
   L+=T*disk*limb*14718.*uEclipse;
 }
 color=vec4(L,1);
}
