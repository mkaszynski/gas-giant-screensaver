#version 300 es
precision highp float;
flat in float flux;
flat in vec3 starColor,direction;
flat in vec2 center;
out vec4 color;
uniform highp sampler2D uSky,uTrans;
uniform float uDarkGain,uCelestialGain,uStarSigma,uStarFluxScale;
#include "atmosphere.glsl"
uniform float uObserverHeight;
float erfApprox(float x){float s=sign(x);x=abs(x);float t=1./(1.+.3275911*x);return s*(1.-(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-.284496736)*t+.254829592)*t*exp(-x*x));}
void main(){
 // Pixel-integrated optical profile: moving subpixel stars retain their flux.
 vec2 p=(gl_FragCoord.xy-center)*(.707106781/uStarSigma);
 vec2 h=vec2(.353553391/uStarSigma);
 vec2 area=.5*vec2(erfApprox(p.x+h.x)-erfApprox(p.x-h.x),erfApprox(p.y+h.y)-erfApprox(p.y-h.y));
 float shape=max(0.,area.x*area.y);
 vec3 sky=texture(uSky,skyUV(direction)).rgb;
 float night=1.-smoothstep(.00002,.003,dot(sky,vec3(.2126,.7152,.0722)));
 color=vec4(starColor*shape*flux*uStarFluxScale*uCelestialGain/uDarkGain*night*transmission(uTrans,uObserverHeight,direction.y),0);
}
