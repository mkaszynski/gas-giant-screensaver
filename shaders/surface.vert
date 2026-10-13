#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 properties;
layout(location=3) in vec2 materialPosition;
uniform mat3 uMeshFrame;
out vec2 vMaterial;
uniform vec3 uEye,uForward,uRight,uUp;
uniform float uTanFov;
uniform vec2 uResolution;
out vec3 vPosition,vNormal,vProperties,vMesh;
void main(){
 vec3 p=uMeshFrame*(position-uEye);vPosition=p;vNormal=uMeshFrame*normal;vProperties=properties;vMesh=position;vMaterial=materialPosition;
 float z=dot(p,uForward);float near=1.,far=220000.;
 gl_Position=vec4(dot(p,uRight)/(uTanFov*uResolution.x/uResolution.y),dot(p,uUp)/uTanFov,
                 (far+near)/(far-near)*z-2.*far*near/(far-near),z);
}
