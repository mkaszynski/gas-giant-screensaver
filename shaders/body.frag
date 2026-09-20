#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform vec4 uBody;
uniform vec3 uAxisX,uAxisY,uAxisZ;
uniform sampler2D uGiant;
uniform int uMaterial,uOccluderCount;
uniform vec4 uOccluders[21];
uniform float uSpin;
float hash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float noise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);}
float fbm(vec3 p){return .5*noise(p)+.25*noise(p*2.03)+.125*noise(p*4.07)+.0625*noise(p*8.11);}
float visibleSun(vec3 p){
 float vis=1.;const float sr=.00465047;
 for(int i=0;i<21;++i){if(i>=uOccluderCount)break;vec3 v=uOccluders[i].xyz-p;float z=dot(v,uSun);if(z<=0.)continue;float dist=length(v);float r=asin(min(.999,uOccluders[i].w/dist));float d=acos(clamp(dot(v/dist,uSun),-1.,1.));
 if(d>=sr+r)continue;if(r>=d+sr)return 0.;if(sr>=d+r){vis=min(vis,1.-r*r/(sr*sr));continue;}
 float a=acos(clamp((d*d+sr*sr-r*r)/(2.*d*sr),-1.,1.));float b=acos(clamp((d*d+r*r-sr*sr)/(2.*d*r),-1.,1.));float area=sr*sr*a+r*r*b-.5*sqrt(max(0.,(-d+sr+r)*(d+sr-r)*(d-sr+r)*(d+sr+r)));vis=min(vis,1.-area/(PI*sr*sr));}
 return max(0.,vis);
}
void main(){
 vec3 d=ray();float impact=length(cross(uBody.xyz,d));float edge=max(fwidth(impact),uBody.w*.0001);
 float coverage=1.-smoothstep(uBody.w-edge,uBody.w+edge,impact);if(coverage<=0.)discard;
 float hit=dot(uBody.xyz,d)-sqrt(max(0.,uBody.w*uBody.w-impact*impact));
 vec3 point=d*hit,n=normalize(point-uBody.xyz);
 vec3 sn=vec3(dot(n,uAxisX),dot(n,uAxisY),dot(n,uAxisZ));
 vec3 albedo;
 if(uMaterial==0){vec2 st=vec2(atan(sn.z,sn.x)/(2.*PI)+.5-uSpin/(2.*PI),acos(clamp(sn.y,-1.,1.))/PI);albedo=pow(texture(uGiant,st).rgb,vec3(2.2));}
 else {float f=fbm(sn*9.+float(uMaterial)*19.);float fine=noise(sn*180.);vec3 a=vec3(.28,.25,.22),b=vec3(.62,.58,.49);if(uMaterial==2){a=vec3(.3,.39,.42);b=vec3(.83,.86,.82);}if(uMaterial==3){a=vec3(.3,.16,.09);b=vec3(.6,.43,.29);}albedo=mix(a,b,smoothstep(.2,.75,f))*(.85+.15*fine);if(uMaterial==4){albedo=mix(vec3(.014,.045,.09),vec3(.11,.19,.09),smoothstep(.43,.5,f));albedo=mix(albedo,vec3(.75),smoothstep(.72,.86,abs(sn.y)));}}
 float mu=max(0.,dot(n,uSun));float eclipse=visibleSun(point+n*.00001);
 // Lambertian direct reflection, faint planetshine on moons, no artificial light on giant's night side.
 vec3 L=albedo*(mu*eclipse/PI+ (uMaterial==0?vec3(.0000003):vec3(.5,.65,1.)*uPlanetLight*.08));
 if(uMaterial==0) L+=vec3(.13,.21,.3)*pow(1.-max(0.,dot(n,-d)),5.)*pow(mu,.35)*eclipse*.045;
 color=vec4((sky(d)+viewT(d)*L)*coverage,coverage);
}
