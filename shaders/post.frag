#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform highp sampler2D uScene,uSurface;
uniform bool uMovingSurface;
uniform float uSurfaceFraction;
uniform float uOpacity;
uniform float uSceneScale;
#include "clouds.glsl"
vec3 sceneColor(vec2 p){return texture(uScene,p).rgb/uSceneScale;}
// Wrap stationary spatial coordinates before adding small wind offsets.
// Adding wind to a large grazing ray coordinate discards subframe precision.
vec3 tonemap(vec3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);}
#include "terrain.glsl"
vec4 displayColor(vec3 L){
 vec3 mapped=pow(tonemap(L*uExposure),vec3(1./2.2));
 float dither=(fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)-.5)/255.;
 return vec4((mapped+dither)*uOpacity,uOpacity);
}
void main(){
 vec4 surface=vec4(0.);
 if(uMovingSurface&&uv.y<=uSurfaceFraction){
  surface=texture(uSurface,vec2(uv.x,uv.y/uSurfaceFraction));
  // All four lanes must agree before skipping derivative-dependent sky work.
  // Fetch aligned coverage texels; partial silhouettes keep the full sky pass.
  ivec2 q=ivec2(floor(gl_FragCoord.xy*.5))*2;
  ivec2 size=textureSize(uSurface,0);
  if(q.x+1<size.x&&q.y+1<size.y){
   float coverage=min(min(texelFetch(uSurface,q,0).a,texelFetch(uSurface,q+ivec2(1,0),0).a),
                      min(texelFetch(uSurface,q+ivec2(0,1),0).a,texelFetch(uSurface,q+ivec2(1,1),0).a));
   if(coverage>=1.){color=displayColor(surface.rgb/uSceneScale);return;}
  }
 }
 vec3 d=ray();vec3 L=sceneColor(uv);
 L=cloudScene(d,L);
 // Compact optical point-spread approximation. No blur pyramid or extra fullscreen passes.
 // Solar visibility includes eclipses; ridge/cloud coverage is evaluated at the light source.
 float solarVisibility=uEclipse*smoothstep(-.006,.008,uSun.y);
 float sunZ=dot(uSun,uForward);
 if(sunZ>0.)solarVisibility*=1.-terrainSample(uSun).a;
 vec3 sunCloudPoint=uSun*sphere(vec3(0,Rg+uObserverHeight,0),uSun,Rg+8.).y;
 solarVisibility*=1.-cloudDensity(sunCloudPoint,uSun.y);
 float sunAngle=acos(clamp(dot(d,uSun),-1.,1.));
 float halo=.42*exp(-.5*pow(sunAngle/.006,2.))+.035*exp(-.5*pow(sunAngle/.024,2.));
 L+=transmission(uTrans,uObserverHeight,uSun.y)*halo*solarVisibility;
 // Terrain coverage is premultiplied before mips, with no thresholding.
 if(uMovingSurface){
  L=L*(1.-surface.a)+surface.rgb/uSceneScale;
 }else{
 vec4 mountain=terrainSample(d);
 vec3 terrain=terrainSurface(d,mountain);
 if(mountain.a>0.){
 float ridgeCoverage=mountain.a;
 L=ridgeCoverage>=1.?terrain:mix(L,terrain,ridgeCoverage);}
 }
 color=displayColor(L);
}
