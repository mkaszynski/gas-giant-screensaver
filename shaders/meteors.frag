#version 300 es
precision highp float;
flat in vec4 endpoints,light;
layout(location=0)out vec4 color;
layout(location=1)out vec4 skyWeather;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform float uCelestialGain;
uniform bool uMeteorManual,uMeteorReveal;
uniform highp sampler2D uMeteorSurface;
uniform float uMeteorSurfaceFraction;
float erfApprox(float x){float s=sign(x);x=abs(x);float t=1./(1.+.3275911*x);return s*(1.-(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-.284496736)*t+.254829592)*t*exp(-x*x));}
float opticalLine(vec2 p,vec2 a,vec2 b,float sigma){
 // Gaussian convolution of a finite, uniformly exposed line. The pixel box's
 // variance is included so even fractional-width, moving streaks retain flux.
 float variance=sigma*sigma+1./12.;vec2 v=b-a;float len=length(v);
 if(len<.01)return exp(-dot(p-a,p-a)/(2.*variance))/(2.*PI*variance);
 vec2 axis=v/len;vec2 d=p-a;
 float across=dot(d,vec2(-axis.y,axis.x)),along=dot(d,axis);
 float root=sqrt(2.*variance);
 float ends=.5*(erfApprox(along/root)-erfApprox((along-len)/root));
 return max(0.,ends)*exp(-across*across/(2.*variance))/(sqrt(2.*PI*variance)*len);
}
void main(){
 vec3 d=ray();
 float profile=.98*opticalLine(gl_FragCoord.xy,endpoints.xy,endpoints.zw,light.w)
              +.02*opticalLine(gl_FragCoord.xy,endpoints.xy,endpoints.zw,sqrt(light.w*light.w+2.));
 float night=1.-smoothstep(.00002,.003,dot(texture(uSky,skyUV(d)).rgb,vec3(.2126,.7152,.0722)));
 float visibility=uMeteorManual?1.:night;
 // Manual review must stay visible through every sky/giant phase after a
 // seek. Automatic showers retain calibrated contrast adaptation.
 float gain=uMeteorManual?1.:uCelestialGain;
 vec3 L=light.rgb*profile*visibility*gain/uDarkGain*viewT(d);
 // A forced daylight streak can overlap the saturated solar disk. Bounded
 // additive blending stays monotonic and finite in the half-float HDR target.
 float headroom=uMeteorManual?clamp(max(L.r,max(L.g,L.b))*uSceneScale/65504.,0.,1.):0.;
 color=vec4(L,headroom);skyWeather=vec4(0.);
 if(uMeteorReveal){
  skyWeather=vec4(min(L*uSceneScale,vec3(60000.)),headroom);
  // Ground remains opaque. Partial MSAA silhouettes use the separate sky
  // weather attachment and receive their original coverage in the post pass.
  if(gl_FragCoord.y<uResolution.y*uMeteorSurfaceFraction&&
     texelFetch(uMeteorSurface,ivec2(gl_FragCoord.xy),0).a>0.)color=vec4(0.);
 }
}
