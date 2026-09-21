uniform int uRingOccluderCount;
uniform vec4 uRingOccluders[21];
vec4 ringLayer(vec3 d,float maximumDistance){
 if(!uRingsEnabled)return vec4(0.);
 float denom=dot(d,uRingNormal);
 float safeDenom=abs(denom)<1.e-7?(denom<0.?-1.e-7:1.e-7):denom;
 float hit=dot(uRingCenter,uRingNormal)/safeDenom;
 vec3 point=d*hit;float radius=length(point-uRingCenter);
 // Derivatives must precede divergent hit/radius rejection; otherwise edge
 // quads produce undefined mip levels and sparkle as the disk moves.
 float footprint=max(fwidth(radius),1.e-7);
 float lod=max(0.,log2(footprint*4096./(uRingBounds.y-uRingBounds.x)));
 // Filter the projected annulus as two implicit conics. Unlike a radial
 // cutoff after ray/plane division, this remains well behaved at grazing
 // angles without a hard pixel-center cutoff.
 vec3 projected=d*dot(uRingCenter,uRingNormal)-uRingCenter*denom;
 float squared=dot(projected,projected),denom2=denom*denom;
 float outer=uRingBounds.y*uRingBounds.y*denom2-squared;
 float inner=uRingBounds.x*uRingBounds.x*denom2-squared;
 float outerWidth=max(fwidth(outer),1.e-12),innerWidth=max(fwidth(inner),1.e-12);
 float coverage=max(0.,smoothstep(-.5*outerWidth,.5*outerWidth,outer)-
                         smoothstep(-.5*innerWidth,.5*innerWidth,inner));
 if(abs(denom)<1.e-7||hit<=0.||hit>=maximumDistance||coverage<=0.)return vec4(0.);
 float tau=ringTau(clamp(radius,uRingBounds.x,uRingBounds.y),lod);
 if(tau<=0.)return vec4(0.);
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
 float sunlight=sphereSunVisibility(point,uRingOccluderCount,uRingOccluders);
 float particleAlbedo=textureLod(uRingProfile,vec2((radius-uRingBounds.x)/(uRingBounds.y-uRingBounds.x),.5),lod).g;
 vec3 ice=vec3(.92,.94,.97)*particleAlbedo;
 vec3 reflectedLight=ice*(phase*max(0.,scattering)/(4.*PI))*sunlight;
 if(reflected)reflectedLight+=ice*.12*alpha*mu0/(mu0+.3)/PI*sunlight;
 // Low planetshine keeps shadowed ice faint, without making it self-luminous.
 reflectedLight+=ice*vec3(.5,.65,1.)*uPlanetLight*.0006*alpha;
 return vec4(sky(d)*alpha+viewT(d)*reflectedLight,alpha)*coverage;
}
