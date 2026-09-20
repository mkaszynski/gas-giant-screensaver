#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
void main(){vec3 d=ray();vec3 L=sky(d);float angle=acos(clamp(dot(d,uSun),-1.,1.));float aa=uTanFov/uResolution.y;
 float disk=1.-smoothstep(.00465047-aa,.00465047+aa,angle);
 // Solar radiance is integrated through the atmosphere; tiny bloom is added by tone mapping.
 float radiusFraction=clamp(angle/.00465047,0.,1.);
 float limb=.4+.6*sqrt(max(0.,1.-radiusFraction*radiusFraction));
 L+=viewT(d)*disk*limb*14718.*uEclipse;
 color=vec4(L,1);
}
