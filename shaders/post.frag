#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform sampler2D uScene,uMountains,uNoise;
uniform float uOpacity;
float n2(vec2 p){return texture(uNoise,p/256.).r;}
float clouds(vec2 p){return .55*n2(p)+.27*n2(p*2.01)+.12*n2(p*4.03)+.06*n2(p*8.07);}
vec3 tonemap(vec3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);}
void main(){vec3 d=ray();vec3 L=texture(uScene,uv).rgb;
 // Thin curved cloud sheet, with apparent wind motion and sun-dependent silver edges.
 vec2 roots=sphere(vec3(0,Rg+.2,0),d,Rg+8.);float dist=roots.y;vec3 q=d*dist;
 vec2 p=q.xz*vec2(.85,1.9)+vec2(uTime*.007,uTime*.003);
 float density=smoothstep(.55,.79,clouds(p));density*=smoothstep(.02,.15,d.y)*.26;
 density*=smoothstep(.33,.66,n2(p*.045+vec2(87.,31.)));
 float sunlit=smoothstep(-.12,.06,uSun.y)*uEclipse;
 vec3 cloudL=sky(d)*1.4+transmission(uTrans,8.,uSun.y)*sunlit*.095+vec3(.00002)*uPlanetLight;
 L=mix(L,cloudL,density);
 // Compact optical point-spread approximation. No blur pyramid or extra fullscreen passes.
 // Solar visibility includes eclipses; ridge/cloud coverage is evaluated at the light source.
 float solarVisibility=uEclipse*smoothstep(-.006,.008,uSun.y);
 float sunZ=dot(uSun,uForward);
 if(sunZ>0.){
   vec2 sunScreen=vec2(dot(uSun,uRight)/(sunZ*uTanFov*uResolution.x/uResolution.y),dot(uSun,uUp)/(sunZ*uTanFov))*.5+.5;
   if(sunScreen.x>=0.&&sunScreen.x<=1.&&sunScreen.y>=0.&&sunScreen.y<.30){
     float ridgeAlpha=texture(uMountains,vec2(sunScreen.x,1.-sunScreen.y/.30)).a;
     solarVisibility*=1.-smoothstep(.01,.12,ridgeAlpha);
   }
 }
 vec3 sunCloudPoint=uSun*sphere(vec3(0,Rg+.2,0),uSun,Rg+8.).y;
 vec2 sunCloudUV=sunCloudPoint.xz*vec2(.85,1.9)+vec2(uTime*.007,uTime*.003);
 float sunCloud=smoothstep(.55,.79,clouds(sunCloudUV))*smoothstep(.02,.15,uSun.y)*.26*smoothstep(.33,.66,n2(sunCloudUV*.045+vec2(87.,31.)));
 solarVisibility*=1.-sunCloud;
 float sunAngle=acos(clamp(dot(d,uSun),-1.,1.));
 float halo=.42*exp(-.5*pow(sunAngle/.006,2.))+.035*exp(-.5*pow(sunAngle/.024,2.));
 L+=transmission(uTrans,.2,uSun.y)*halo*solarVisibility;
 // The ridge is a shallow foreground strip. Alpha is retained from the original asset.
 vec2 mt=vec2(uv.x,(1.-uv.y/.30));
 if(uv.y<.30){vec4 mountain=texture(uMountains,clamp(mt,0.,1.));vec3 albedo=pow(mountain.rgb,vec3(2.2));
 vec3 light=vec3(.000040)+vec3(.10,.16,.24)*uPlanetLight*.10;
 light+=transmission(uTrans,.2,max(.01,uSun.y))*max(0.,uSun.y)*uEclipse*.20;
 light+=sky(vec3(0,1,0))*.45;
 vec3 terrain=albedo*light;
 // Generated alpha may be translucent inside the ridge. Only its silhouette is a coverage mask.
 float ridgeCoverage=smoothstep(.01,.12,mountain.a);
 L=ridgeCoverage>=1.?terrain:mix(L,terrain,ridgeCoverage);}
 vec3 mapped=pow(tonemap(L*uExposure),vec3(1./2.2));
 float dither=(fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)-.5)/255.;
 color=vec4((mapped+dither)*uOpacity,uOpacity);
}
