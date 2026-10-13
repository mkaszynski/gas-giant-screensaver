#version 300 es
precision highp float;
layout(location=0) in vec3 center;
layout(location=1) in vec3 properties;
uniform vec3 uEye,uTreeRight,uForward,uRight,uUp;
uniform mat3 uMeshFrame;
uniform mat4 uTreeShadowMatrix;
uniform bool uShadowPass;
uniform float uTanFov;
uniform vec2 uResolution;
out vec2 vUV;
out vec3 vPosition,vMesh;
out float vVariation;
void main(){
 vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
 vec2 q=corners[gl_VertexID];float height=properties.x;
 vec3 p=center+uTreeRight*((q.x-.5)*height*.75)+vec3(0,q.y*height,0);vMesh=p;
 vUV=vec2((q.x+properties.y*4.+min(3.,floor(properties.z*4.)))/8.,1.-q.y);vVariation=properties.z;
 if(uShadowPass){gl_Position=uTreeShadowMatrix*vec4(p,1.);vPosition=p;return;}
 p=uMeshFrame*(p-uEye);vPosition=p;float z=dot(p,uForward);float near=1.,far=220000.;
 gl_Position=vec4(dot(p,uRight)/(uTanFov*uResolution.x/uResolution.y),dot(p,uUp)/uTanFov,(far+near)/(far-near)*z-2.*far*near/(far-near),z);
}
