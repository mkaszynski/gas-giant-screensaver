#version 300 es
precision highp float;
in vec2 vUV;
in vec3 vPosition,vMesh;
in float vVariation;
out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
uniform sampler2D uTrees;
uniform highp sampler2DShadow uShadowNear,uShadowFar;
uniform mat4 uShadowMatrix[2];
uniform float uSceneScale;
void main(){
 vec4 tree=texture(uTrees,vUV);tree.a*=1.-smoothstep(1450.,1750.,length(vPosition.xz));if(tree.a<.005)discard;
 vec3 d=normalize(vPosition),N=normalize(vec3(-uForward.x,.4,-uForward.z));
 vec3 near=(uShadowMatrix[0]*vec4(vMesh,1.)).xyz*.5+.5;
 vec3 far=(uShadowMatrix[1]*vec4(vMesh,1.)).xyz*.5+.5;
 float shadow=1.;
 if(uSun.y>0.){near.z-=.0008;far.z-=.0001;
  shadow=all(greaterThan(near,vec3(.01)))&&all(lessThan(near,vec3(.99)))?texture(uShadowNear,near):texture(uShadowFar,far);}
 vec3 light=transmission(uTrans,uObserverHeight,uSun.y)*max(.08,dot(N,uSun))*.24*uEclipse*shadow*smoothstep(0.,.02,uSun.y);
 light+=sky(vec3(0,1,0))*1.9+vec3(.10,.16,.24)*uPlanetLight*.04+vec3(.00004)/uDarkGain;
 vec3 L=tree.rgb*mix(.75,1.1,vVariation)*light;
 vec3 T=exp(-length(vPosition)*vec3(.000020,.000031,.000047));L=L*T+sky(d)*(1.-T);
 color=vec4(min(L*uSceneScale,vec3(60000.))*tree.a,tree.a);
}
