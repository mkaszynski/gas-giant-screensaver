#version 300 es
precision highp float;
in vec2 uv;out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "sphere_shadow.glsl"
#include "ring_profile.glsl"
void main(){
 vec3 d=ray();float denom=dot(d,uRingNormal);
 if(abs(denom)<1.e-7)discard;
 float hit=dot(uRingCenter,uRingNormal)/denom;
 if(hit<=0.||hit>=256.)discard;
 vec3 point=d*hit;float radius=length(point-uRingCenter);
 if(radius<uRingBounds.x||radius>uRingBounds.y)discard;
 float lod=max(0.,log2(max(fwidth(radius),1.e-7)*4096./(uRingBounds.y-uRingBounds.x)));
 float tau=ringTau(radius,lod);
 if(tau<=0.)discard;
 float mu=max(abs(denom),.0001),mu0=max(abs(dot(uRingNormal,uSun)),.0001);
 float alpha=1.-exp(-tau/mu);
 // Single-scattering slab: reflected and transmitted sides have different
 // brightness. Density controls both the visible transparency and shadows.
 bool reflected=dot(uRingNormal,-d)*dot(uRingNormal,uSun)>0.;
 float scattering;
 if(reflected)scattering=mu0/(mu+mu0)*(1.-exp(-tau*(1./mu+1./mu0)));
 else if(abs(mu0-mu)<.001)scattering=tau/mu*exp(-tau/mu);
 else scattering=mu0/(mu0-mu)*(exp(-tau/mu0)-exp(-tau/mu));
 float phase=1.2+1.4*pow(max(0.,dot(-d,uSun)),4.)+.45*pow(max(0.,dot(d,uSun)),12.);
 float sunlight=sphereSunVisibility(point);
 float particleAlbedo=textureLod(uRingProfile,vec2((radius-uRingBounds.x)/(uRingBounds.y-uRingBounds.x),.5),lod).g;
 vec3 ice=vec3(.92,.94,.97)*particleAlbedo;
 vec3 reflectedLight=ice*(phase*max(0.,scattering)/(4.*PI))*sunlight;
 if(reflected)reflectedLight+=ice*.12*alpha*mu0/(mu0+.3)/PI*sunlight;
 // Low planetshine keeps shadowed ice faint, without making it self-luminous.
 reflectedLight+=ice*vec3(.5,.65,1.)*uPlanetLight*.0006*alpha;
 gl_FragDepth=hit/256.;
 color=vec4(sky(d)*alpha+viewT(d)*reflectedLight,alpha);
}
