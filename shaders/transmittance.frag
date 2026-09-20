#version 300 es
precision highp float;
in vec2 uv; out vec4 color;
#include "atmosphere.glsl"
void main(){
 float h=max(.001,uv.y*100.), mu=uv.x*2.-1.;vec3 p=vec3(0,Rg+h,0),d=vec3(sqrt(max(0.,1.-mu*mu)),mu,0);
 vec2 ground=sphere(p,d,Rg); if(ground.x>0.){color=vec4(0,0,0,1);return;}
 float dist=sphere(p,d,Rt).y;vec3 optical=vec3(0);
 for(int i=0;i<64;++i){vec3 s,e,r,m;medium(length(p+d*(float(i)+.5)*dist/64.)-Rg,s,e,r,m);optical+=e*dist/64.;}
 color=vec4(exp(-optical),1);
}
