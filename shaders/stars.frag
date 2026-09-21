#version 300 es
precision highp float;
in float brightness;in vec3 direction;out vec4 color;
uniform sampler2D uSky,uTrans;uniform float uExposure;
#include "atmosphere.glsl"
void main(){vec2 p=gl_PointCoord*2.-1.;float g=exp(-dot(p,p)*5.);vec3 sky=texture(uSky,skyUV(direction)).rgb;float night=1.-smoothstep(.00002,.003,dot(sky,vec3(.21,.72,.07)));vec3 c=mix(vec3(.7,.8,1.),vec3(1.,.86,.69),fract(brightness*53.));color=vec4(c*g*pow(brightness,3.)*.035*night*transmission(uTrans,.2,direction.y),0);}
