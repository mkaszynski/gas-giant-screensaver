#version 300 es
precision highp float;
layout(location=0)in vec4 aStar;
layout(location=1)in vec4 aColor;
uniform mat3 uStarFrame;
uniform vec3 uForward,uRight,uUp;
uniform vec2 uResolution;
uniform float uTanFov,uStarSize;
flat out float flux;
flat out vec3 starColor,direction;
flat out vec2 center;
void main(){
 vec3 d=uStarFrame*aStar.xyz;direction=d;
 float z=dot(d,uForward);
 vec2 p=vec2(dot(d,uRight),dot(d,uUp))/(max(.0001,z)*uTanFov);
 p.x/=uResolution.x/uResolution.y;
 gl_Position=vec4(z>0.?p:vec2(3.),0,1);
 center=(p*.5+.5)*uResolution;
 flux=aStar.w;starColor=aColor.rgb;
 gl_PointSize=uStarSize;
}
