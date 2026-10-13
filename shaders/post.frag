#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform highp sampler2D uScene,uSurface;
uniform bool uMovingSurface;
uniform float uSurfaceFraction;
uniform highp sampler2D uNoise;
uniform float uOpacity;
uniform float uSceneScale;
uniform vec2 uCloudOffset[5];
uniform float uCloudLongitudeScale;
vec3 sceneColor(vec2 p){return texture(uScene,p).rgb/uSceneScale;}
// Wrap stationary spatial coordinates before adding small wind offsets.
// Adding wind to a large grazing ray coordinate discards subframe precision.
float n2(vec2 p,vec2 wind){return texture(uNoise,(mod(p,256.)+wind)/256.).r;}
float clouds(vec2 p){return .55*n2(p,uCloudOffset[0])+.27*n2(p*2.01,uCloudOffset[1])+.12*n2(p*4.03,uCloudOffset[2])+.06*n2(p*8.07,uCloudOffset[3]);}
vec3 tonemap(vec3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);}
vec3 cloudScene(vec3 d,vec3 L){
 // Thin curved cloud sheet, with apparent wind motion and sun-dependent silver edges.
 vec2 roots=sphere(vec3(0,Rg+uObserverHeight,0),d,Rg+8.);float dist=roots.y;vec3 q=d*dist;
 vec2 p=q.xz*vec2(.85*uCloudLongitudeScale,1.9);
 float density=smoothstep(.55,.79,clouds(p));density*=smoothstep(.02,.15,d.y)*.26;
 density*=smoothstep(.33,.66,n2(p*.045+vec2(87.,31.),uCloudOffset[4]));
 float sunlit=smoothstep(-.12,.06,uSun.y)*uEclipse;
 vec3 cloudL=sky(d)*1.4+transmission(uTrans,8.,uSun.y)*sunlit*.095+vec3(.00002)*uPlanetLight;
 return mix(L,cloudL,density);
}
#include "terrain.glsl"
void main(){vec3 d=ray();vec3 L=sceneColor(uv);
 L=cloudScene(d,L);
 // Compact optical point-spread approximation. No blur pyramid or extra fullscreen passes.
 // Solar visibility includes eclipses; ridge/cloud coverage is evaluated at the light source.
 float solarVisibility=uEclipse*smoothstep(-.006,.008,uSun.y);
 float sunZ=dot(uSun,uForward);
 if(sunZ>0.)solarVisibility*=1.-terrainSample(uSun).a;
 vec3 sunCloudPoint=uSun*sphere(vec3(0,Rg+uObserverHeight,0),uSun,Rg+8.).y;
 vec2 sunCloudUV=sunCloudPoint.xz*vec2(.85*uCloudLongitudeScale,1.9);
 float sunCloud=smoothstep(.55,.79,clouds(sunCloudUV))*smoothstep(.02,.15,uSun.y)*.26*smoothstep(.33,.66,n2(sunCloudUV*.045+vec2(87.,31.),uCloudOffset[4]));
 solarVisibility*=1.-sunCloud;
 float sunAngle=acos(clamp(dot(d,uSun),-1.,1.));
 float halo=.42*exp(-.5*pow(sunAngle/.006,2.))+.035*exp(-.5*pow(sunAngle/.024,2.));
 L+=transmission(uTrans,uObserverHeight,uSun.y)*halo*solarVisibility;
 // Terrain coverage is premultiplied before mips, with no thresholding.
 if(uMovingSurface){
  vec4 surface=texture(uSurface,vec2(uv.x,uv.y/uSurfaceFraction));
  if(uv.y>uSurfaceFraction)surface=vec4(0.);
  L=L*(1.-surface.a)+surface.rgb/uSceneScale;
 }else{
 vec4 mountain=terrainSample(d);
 vec3 terrain=terrainSurface(d,mountain);
 if(mountain.a>0.){
 float ridgeCoverage=mountain.a;
 L=ridgeCoverage>=1.?terrain:mix(L,terrain,ridgeCoverage);}
 }
 vec3 mapped=pow(tonemap(L*uExposure),vec3(1./2.2));
 float dither=(fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)-.5)/255.;
 color=vec4((mapped+dither)*uOpacity,uOpacity);
}
