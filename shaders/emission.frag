#version 300 es
precision highp float;
in vec3 point,radiance;
in vec4 profile;
out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "ring_profile.glsl"
uniform int uEmissionOccluderCount;
uniform vec4 uEmissionOccluders[21];
// Integral filtering retains the flux of subpixel flashes and narrow arcs.
float erfApprox(float x){float s=sign(x);x=abs(x);float t=1./(1.+.3275911*x);return s*(1.-(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-.284496736)*t+.254829592)*t*exp(-x*x));}
float primitive(float x){return 1.253314137*erfApprox(x*.707106781);}
float secondPrimitive(float x){return x*primitive(x)+exp(-.5*x*x);}
float filteredLine(float q,float a,float b){
 a=abs(a);b=abs(b);if(a<b){float t=a;a=b;b=t;}
 if(a<.03)return exp(-.5*q*q);
 if(b<.03)return max(0.,(primitive(q+.5*a)-primitive(q-.5*a))/a);
 return max(0.,(secondPrimitive(q+.5*(a+b))-secondPrimitive(q+.5*(a-b))-secondPrimitive(q+.5*(-a+b))+secondPrimitive(q-.5*(a+b)))/(a*b));
}
float pixelCDF(float x,float a,float b){
 a=abs(a);b=abs(b);if(a<b){float t=a;a=b;b=t;}
 float h=.5*(a+b);
 if(x<=-h)return 0.;if(x>=h)return 1.;
 if(b<1.e-5)return clamp(x/max(a,1.e-5)+.5,0.,1.);
 float t=x+h;
 if(t<b)return t*t/(2.*a*b);
 if(t<a)return (t-.5*b)/a;
 return 1.-(a+b-t)*(a+b-t)/(2.*a*b);
}
void main(){
 vec3 d=ray();float dist=length(point);
 float shape;
 if(profile.z>.5){
   shape=filteredLine(profile.x,dFdx(profile.x),0.)*filteredLine(profile.y,dFdy(profile.y),0.);
 }else {
   shape=filteredLine(profile.x,dFdx(profile.x),dFdy(profile.x));
   float a=dFdx(profile.y),b=dFdy(profile.y);
   shape*=max(0.,pixelCDF(profile.w-profile.y,a,b)-pixelCDF(-profile.w-profile.y,a,b));
 }
 // Emission has no solar-light multiplier: shadows do not switch it off.
 // Geometric blockers, including the planet itself, still obscure photons.
 float visible=1.;
 for(int i=0;i<21;++i){
   if(i>=uEmissionOccluderCount)break;
   vec4 body=uEmissionOccluders[i];float along=dot(body.xyz,d);
   float impact=length(cross(body.xyz,d));
   float width=max(fwidth(impact),1.e-6);
   float nearHit=along-sqrt(max(0.,body.w*body.w-impact*impact));
   if(i==0){
     float coverage=1.-smoothstep(body.w-.5*width,body.w+.5*width,impact);
     if(profile.z>.5){visible*=coverage;dist=nearHit;}
     else {
       // The ribbon's centerline depth cannot be reused for its expanded
       // antialiasing vertices: inward subpixels hit the shell sooner.
       bool front=dot(point-body.xyz,d)<0.;
       float shell=body.w+120./71492.;
       float halfPath=sqrt(max(0.,shell*shell-impact*impact));
       dist=along+(front?-halfPath:halfPath);
       if(!front)visible*=1.-coverage;
     }
     continue;
   }
   if(along>0.&&nearHit<dist-.00003)
     visible*=smoothstep(body.w-.5*width,body.w+.5*width,impact);
 }
 float denom=dot(d,uRingNormal);
 float ringHit=dot(uRingCenter,uRingNormal)/(abs(denom)>1.e-7?denom:1.e-7);
 float radius=length(d*ringHit-uRingCenter);
 float footprint=max(fwidth(radius),1.e-7);
 float lod=max(0.,log2(footprint*4096./(uRingBounds.y-uRingBounds.x)));
 vec3 projected=d*dot(uRingCenter,uRingNormal)-uRingCenter*denom;
 float squared=dot(projected,projected),denom2=denom*denom;
 float outer=uRingBounds.y*uRingBounds.y*denom2-squared;
 float inner=uRingBounds.x*uRingBounds.x*denom2-squared;
 float coverage=max(0.,smoothstep(-.5*max(fwidth(outer),1.e-12),.5*max(fwidth(outer),1.e-12),outer)-smoothstep(-.5*max(fwidth(inner),1.e-12),.5*max(fwidth(inner),1.e-12),inner));
 if(uRingsEnabled&&abs(denom)>1.e-7&&ringHit>0.&&ringHit<dist)
   visible*=1.-coverage+coverage*exp(-ringTau(clamp(radius,uRingBounds.x,uRingBounds.y),lod)/max(abs(denom),.0001));
 // Requested visibility boost: five times the modeled visible radiance.
 color=vec4(5.*radiance*max(0.,shape)*visible*viewT(d),0.);
}
