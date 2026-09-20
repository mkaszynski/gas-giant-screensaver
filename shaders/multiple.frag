#version 300 es
precision highp float;
in vec2 uv; out vec4 color;uniform sampler2D uTrans;
#include "atmosphere.glsl"
// Isotropic higher-order scattering feedback, following Hillaire's LUT architecture.
void main(){
 float h=max(.02,uv.y*100.),mu=uv.x*2.-1.;vec3 p=vec3(0,Rg+h,0),sun=vec3(sqrt(max(0.,1.-mu*mu)),mu,0);
 vec3 lum=vec3(0),feedback=vec3(0);
 for(int k=0;k<32;++k){float y=1.-2.*(float(k)+.5)/32.,a=float(k)*2.39996323;vec3 d=vec3(cos(a)*sqrt(1.-y*y),y,sin(a)*sqrt(1.-y*y));
  float dist=sphere(p,d,Rt).y;vec2 g=sphere(p,d,Rg);if(g.x>0.)dist=g.x;
  vec3 T=vec3(1),l=vec3(0),f=vec3(0);float ds=dist/24.;
  for(int j=0;j<24;++j){vec3 q=p+d*(float(j)+.5)*ds;float alt=length(q)-Rg;vec3 s,e,r,m;medium(alt,s,e,r,m);vec3 stepT=exp(-e*ds);vec3 integral=(1.-stepT)/max(e,vec3(1e-6));vec3 ts=transmission(uTrans,alt,dot(normalize(q),sun));l+=T*integral*s*ts/(4.*PI);f+=T*integral*s;T*=stepT;}
  if(g.x>0.){vec3 q=normalize(p+d*dist);l+=T*.12*max(0.,dot(q,sun))*transmission(uTrans,0.01,dot(q,sun))/PI;}
  lum+=l/32.;feedback+=f/32.;
 }
 color=vec4(lum/max(vec3(.05),1.-feedback),1);
}
