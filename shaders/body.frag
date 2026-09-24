#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform vec4 uBody;
uniform vec3 uAxisX,uAxisY,uAxisZ;
uniform sampler2D uGiant;
uniform int uMaterial;
uniform int uOccluderCount;
uniform vec4 uOccluders[21];
#include "sphere_shadow.glsl"
#include "ring_profile.glsl"
#include "ring_layer.glsl"
#ifdef GIANT_ATMOSPHERE
#include "giant_atmosphere.glsl"
#include "giant_filter.glsl"
#endif
uniform float uSpin;
float hash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float noise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);}
float fbm(vec3 p){return .5*noise(p)+.25*noise(p*2.03)+.125*noise(p*4.07)+.0625*noise(p*8.11);}
void main(){
 vec3 d=ray();float impact=length(cross(uBody.xyz,d));float edge=max(fwidth(impact),uBody.w*.0001);
 vec2 impactFootprint=abs(vec2(dFdx(impact),dFdy(impact)))/uBody.w;
 float coverage=1.-smoothstep(uBody.w-edge,uBody.w+edge,impact);
#ifdef GIANT_INTERIOR
 if(impact>=min(uBody.w*.999,uBody.w-edge*1.5))discard;
#elif defined(GIANT_LIMB)
 if(impact<min(uBody.w*.999,uBody.w-edge*1.5))discard;
#endif
#ifdef GIANT_ATMOSPHERE
 if(coverage<=0.&&impact>uBody.w*(1.+giantTop)+edge)discard;
#else
 if(coverage<=0.)discard;
#endif
 float hit=dot(uBody.xyz,d)-sqrt(max(0.,uBody.w*uBody.w-impact*impact));
 vec3 point=d*hit,n=normalize(point-uBody.xyz);
 vec3 sn=vec3(dot(n,uAxisX),dot(n,uAxisY),dot(n,uAxisZ));
 vec3 albedo;
 // Longitude wraps by a full turn at atan’s branch cut. Correct its
 // gradients before choosing a mip level so that cut cannot become a stripe.
 if(uMaterial==0){vec2 st=vec2(atan(sn.z,sn.x)/(2.*PI)+.5-uSpin/(2.*PI),acos(clamp(sn.y,-1.,1.))/PI);vec2 dx=dFdx(st),dy=dFdy(st);dx.x-=round(dx.x);dy.x-=round(dy.x);albedo=pow(textureGrad(uGiant,st,dx,dy).rgb,vec3(2.2));}
 else {float f=fbm(sn*9.+float(uMaterial)*19.);float fine=noise(sn*180.);vec3 a=vec3(.28,.25,.22),b=vec3(.62,.58,.49);if(uMaterial==2){a=vec3(.3,.39,.42);b=vec3(.83,.86,.82);}if(uMaterial==3){a=vec3(.3,.16,.09);b=vec3(.6,.43,.29);}albedo=mix(a,b,smoothstep(.2,.75,f))*(.85+.15*fine);if(uMaterial==4){albedo=mix(vec3(.014,.045,.09),vec3(.11,.19,.09),smoothstep(.43,.5,f));albedo=mix(albedo,vec3(.75),smoothstep(.72,.86,abs(sn.y)));}}
 float mu=max(0.,dot(n,uSun));float eclipse=sphereSunVisibility(point+n*.00001,uOccluderCount,uOccluders);
 if(mu>0.) eclipse*=ringSunTransmission(point+n*.00001,uSun);
 // Lambertian direct reflection, faint planetshine on moons, no artificial light on giant's night side.
 vec3 L=albedo*(mu*eclipse/PI+ (uMaterial==0?vec3(.0000003)/uDarkGain:vec3(.5,.65,1.)*uPlanetLight*.08));
#ifdef GIANT_ATMOSPHERE
 {
   vec3 sum=vec3(0.);float alpha=0.;
   // Integrate physical radiance over the pixel footprint without expanding
   // the atmosphere. The cloud silhouette uses analytic pixel area coverage.
#ifdef GIANT_INTERIOR
   const bool limb=false;
#else
   bool limb=abs(impact-uBody.w)<edge*2.+uBody.w*giantTop;
#endif
   if(!limb){
     vec3 light;giantAtmosphere(d,impact/uBody.w,L,eclipse,light,alpha);
     sum=sky(d)*alpha+viewT(d)*light;
   }else{
     vec3 light;
     giantFilteredAtmosphere(d,impact/uBody.w,impactFootprint,albedo,eclipse,light,alpha);
     sum=sky(d)*alpha+viewT(d)*light;
   }
   if(alpha<=0.)discard;
   // The ring disk lies outside this thin shell, so it is entirely before
   // or after it. Keep foreground-ring radiance and partial pixel coverage.
   float shellHit=dot(uBody.xyz,d)-sqrt(max(0.,pow(uBody.w*(1.+giantTop),2.)-impact*impact));
   vec4 front=ringLayer(d,shellHit);
   color=vec4(front.rgb*alpha+sum*(1.-front.a),alpha);
   gl_FragDepth=shellHit/256.;
 }
#else
 gl_FragDepth=hit/256.;
 // Rings behind the sphere are already in the background. Composite only
 // the foreground disk here, before sphere coverage, so its antialiased
 // silhouette blends against that background without a depth-buffer fringe.
 vec4 frontRing=ringLayer(d,hit);
 vec3 radiance=frontRing.rgb+(sky(d)+viewT(d)*L)*(1.-frontRing.a);
 color=vec4(radiance*coverage,coverage);
#endif
}
