#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
uniform sampler2D uTrans,uMultiple;uniform vec3 uSun;uniform float uEclipse;
#include "atmosphere.glsl"
void main(){
 vec3 p=vec3(0,Rg+.2,0),d=skyDirection(uv);float dist=sphere(p,d,Rt).y;vec2 g=sphere(p,d,Rg);if(g.x>0.)dist=g.x;
 vec3 T=vec3(1),L=vec3(0);float ds=dist/32.;float c=dot(d,uSun);
 for(int i=0;i<32;++i){vec3 q=p+d*(float(i)+.5)*ds;float h=length(q)-Rg;float mu=dot(normalize(q),uSun);vec3 s,e,r,m;medium(h,s,e,r,m);vec3 stepT=exp(-e*ds);vec3 sunT=transmission(uTrans,h,mu);vec3 ms=texture(uMultiple,vec2(mu*.5+.5,h/100.)).rgb;
 L+=T*(1.-stepT)/max(e,vec3(1e-6))*((r*phaseR(c)+m*phaseM(c))*sunT+s*ms)*uEclipse;T*=stepT;}
 // Faint unresolved starlight/airglow keeps a moonless sky from absolute black.
 // Zenith stays darker; the longer near-horizon path has a gentle blue-green lift.
 float night=1.-smoothstep(-.18,-.06,uSun.y);
 L+=vec3(.000020,.000032,.000050)*night*(1.+.6*pow(1.-abs(d.y),2.));
 color=vec4(L,1);
}
