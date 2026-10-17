#version 300 es
precision highp float;
flat in vec4 endpoints,light;
flat in float taper;
layout(location=0)out vec4 color;
layout(location=1)out vec4 skyWeather;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform float uCelestialGain;
uniform bool uMeteorManual,uMeteorReveal;
uniform highp sampler2D uMeteorSurface;
uniform float uMeteorSurfaceFraction;
float erfApprox(float x){float s=sign(x);x=abs(x);float t=1./(1.+.3275911*x);return s*(1.-(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-.284496736)*t+.254829592)*t*exp(-x*x));}
float normalCDF(float x,float sigma){return .5+.5*erfApprox(x/(sqrt(2.)*sigma));}
float normalPrimitive(float x,float sigma){return x*normalCDF(x,sigma)+sigma/sqrt(2.*PI)*exp(-.5*x*x/(sigma*sigma));}
float pointPixel(vec2 p,float sigma){
 // Integrate the optical PSF over the actual square pixel. A subpixel core
 // retains flux while crossing pixel boundaries without widening the source.
 vec2 hi=.5+.5*vec2(erfApprox((p.x+.5)/(sqrt(2.)*sigma)),erfApprox((p.y+.5)/(sqrt(2.)*sigma)));
 vec2 lo=.5+.5*vec2(erfApprox((p.x-.5)/(sqrt(2.)*sigma)),erfApprox((p.y-.5)/(sqrt(2.)*sigma)));
 return max(0.,hi.x-lo.x)*max(0.,hi.y-lo.y);
}
float crossPixel(float d,vec2 normal,float sigma){
 vec2 box=abs(normal);float a=max(box.x,box.y),b=min(box.x,box.y);
 if(abs(d)>(a+b)*.5+5.5*sigma)return 0.;
 if(b<.05)return max(0.,normalCDF(d+a*.5,sigma)-normalCDF(d-a*.5,sigma))/a;
 // Projection of a square pixel onto a line normal is a trapezoid, not a
 // Gaussian. Its convolution has this analytic four-corner primitive.
 return max(0.,normalPrimitive(d+(a+b)*.5,sigma)-normalPrimitive(d+(a-b)*.5,sigma)
               -normalPrimitive(d+(-a+b)*.5,sigma)+normalPrimitive(d-(a+b)*.5,sigma))/(a*b);
}
float opticalLine(vec2 p,vec2 a,vec2 b,float sigma,float slope){
 vec2 v=b-a;float len=length(v);
 if(len<.001)return pointPixel(p-(a+b)*.5,sigma);
 vec2 axis=v/len;vec2 d=p-a;
 float across=dot(d,vec2(-axis.y,axis.x)),along=dot(d,axis);
 float variance=sigma*sigma+1./12.;
 float root=sqrt(2.*variance);
 float ends=.5*(erfApprox(along/root)-erfApprox((along-len)/root));
 float moment=along*ends+sqrt(variance/(2.*PI))*(exp(-along*along/(2.*variance))-exp(-(along-len)*(along-len)/(2.*variance)));
 moment=clamp(moment,0.,len*max(0.,ends));
 float line=max(0.,(1.-slope)*ends+2.*slope*moment/len)/len*crossPixel(across,vec2(-axis.y,axis.x),sigma);
 if(len>=5.)return line;
 // Short heads need the full two-dimensional pixel footprint. Six bounded
 // Gauss-Legendre samples avoid a discontinuity as their exposure approaches
 // a point. Long wakes use the cheaper analytic expression above.
 const float nodes[6]=float[6](.0337652429,.1693953068,.3806904070,.6193095930,.8306046932,.9662347571);
 const float weights[6]=float[6](.0856622462,.1803807865,.2339569673,.2339569673,.1803807865,.0856622462);
 float shortLine=0.;
 for(int i=0;i<6;++i)shortLine+=pointPixel(d-v*nodes[i],sigma)*weights[i]*(1.+slope*(2.*nodes[i]-1.));
 return mix(shortLine,line,smoothstep(1.5,5.,len));
}
void main(){
 vec3 d=ray();
 float profile=opticalLine(gl_FragCoord.xy,endpoints.xy,endpoints.zw,light.w,taper);
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
