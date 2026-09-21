// Warm H2/He atmosphere above the visible cloud deck. Lengths are in
// Jupiter radii: H = kT/(m g), with T=280 K, m=2.3 u and g=74.36 m/s^2.
// RGB Rayleigh depths approximate a 0.1-bar column at 680/550/440 nm.
uniform sampler2D uGiantSlab;
const float giantH = .0001905; // 13.62 km, NOT an exaggerated visual shell
const float giantTop = 12. * giantH;
const vec3 giantRayleigh = vec3(.0011, .0026, .0065);
const float giantHaze = .00025; // optically thin, nearly neutral aerosol

// Scaled complementary error function for nonnegative x. This form avoids
// overflow in the Chapman (curved exponential atmosphere) column integral.
float erfcScaled(float x) {
 if(x>4.){float a=1./(x*x);return (1.-.5*a+.75*a*a-1.875*a*a*a)/(sqrt(PI)*x);}
 float t=1./(1.+.3275911*x);
 return t*(.254829592+t*(-.284496736+t*(1.421413741+t*(-1.453152027+t*1.061405429))));
}
float giantColumn(vec3 p,vec3 direction) {
 float r=length(p), mu=dot(p,direction)/r;
 float density=exp(-max(0.,r-1.)/giantH);
 float x=mu*sqrt(r/(2.*giantH));
 float scale=sqrt(PI*r/(2.*giantH));
 if(x>=0.)return density*scale*erfcScaled(x);
 float tangent=length(cross(p,direction));
 if(tangent<1.)return 1.e5; // cloud deck blocks the direct beam
 return max(0.,2.*exp(-(tangent-1.)/giantH)*sqrt(PI*tangent/(2.*giantH))-
                  density*scale*erfcScaled(-x));
}
float giantSunFraction(vec3 p) {
 float r=length(p),mu=dot(p,uSun)/r;
 float horizon=-sqrt(max(0.,1.-1./(r*r)));
 // The angular horizon is within four degrees of tangent throughout this
 // shell. Its cosine-space disk radius avoids inverse trig outside penumbrae.
 float x=(mu-horizon)/(.00465047*sqrt(max(.99,1.-horizon*horizon)));
 if(x>=1.)return 1.;if(x<=-1.)return 0.;
 return (acos(-x)+x*sqrt(max(0.,1.-x*x)))/PI;
}
vec3 giantBeam(vec3 p) {
 float visible=giantSunFraction(p);
 if(visible<=0.)return vec3(0.);
 // For a partially visible Sun use its exposed cap's direction for optical
// depth; evaluating the hidden disk center would abruptly black out the rim.
 vec3 radial=normalize(p);
 float horizon=-sqrt(max(0.,1.-1./dot(p,p)));
 vec3 beam=normalize(uSun+radial*max(0.,horizon-dot(radial,uSun)+.002325235));
 return visible*exp(-(giantRayleigh+vec3(giantHaze))*giantColumn(p,beam));
}

// Single scattering with Beer-Lambert extinction. Only the very narrow limb
// needs a curved path integral; most pixels use the thin-shell slab solution.
// No bloom, emission, eclipse brightness multiplier or extra frame buffer.
void giantAtmosphere(vec3 d,float impact,vec3 cloudLight,float cloudShadow,
                     out vec3 radiance,out float opacity) {
 vec3 center=uBody.xyz/uBody.w;
 float along=dot(center,d), top=1.+giantTop;
 float halfPath=sqrt(max(0.,top*top-impact*impact));
 if(impact>=top){radiance=vec3(0.);opacity=0.;return;}
 bool cloud=impact<1.;
 float end=cloud?along-sqrt(max(0.,1.-impact*impact)):along+halfPath;
 float start=along-halfPath;
 vec3 beta=giantRayleigh+vec3(giantHaze);
 float cosine=dot(d,uSun);
 float rayPhase=3.*(1.+cosine*cosine)/(16.*PI);
 const float g=.65;
 float hazePhase=(1.-g*g)/(4.*PI*pow(1.+g*g-2.*g*cosine,1.5));
 vec3 phase=(giantRayleigh*rayPhase+vec3(giantHaze*.98*hazePhase))/beta;
 vec3 scatter=vec3(0.),transmit=vec3(1.),cloudBeam=vec3(0.);
#ifdef GIANT_INTERIOR
 if(true){
#elif defined(GIANT_LIMB)
 if(false){
#else
 if(impact<.999){
#endif
   // Closed-form plane-parallel single scattering, with curved solar column
   // at the layer midpoint. Shadow visibility is sampled above the clouds.
   vec3 surface=d*end-center;
   float viewMu=max(.01,-dot(normalize(surface),d));
   vec3 p=surface+normalize(surface)*giantH;
   float sunMu=dot(normalize(surface),uSun);
#ifdef GIANT_SLAB_LUT
   if(sunMu>.02){
     vec2 st=(sqrt(vec2(sunMu,viewMu))*127.+.5)/128.;
     vec4 slab=texture(uGiantSlab,st);
     vec3 totalT=clamp(1.-slab.rgb/slab.a,0.,1.);
     radiance=phase*slab.rgb*cloudShadow+totalT*cloudLight;
     opacity=1.;return;
   }
#endif
   float column=giantColumn(surface,-d);
   transmit=exp(-beta*column);
   if(sunMu>.02){
     vec3 sunDepth=beta*giantColumn(surface,uSun);
     cloudBeam=exp(-sunDepth);
     vec3 joint=beta*column+sunDepth;
     scatter=phase*(1.-exp(-joint))*beta*column/joint;
   }else {scatter=phase*(1.-transmit)*giantBeam(p);cloudBeam=giantBeam(surface);}
   scatter*=cloudShadow;
 }else{
   const int steps=6;
   float ds=(end-start)/float(steps);
   float densities[steps];float columnSum=0.;
   for(int j=0;j<steps;++j){
     vec3 p=d*(start+(float(j)+.5)*ds)-center;
     densities[j]=exp(-max(0.,length(p)-1.)/giantH)*ds/giantH;
     columnSum+=densities[j];
   }
   // Normalize the quadrature to the curved column. Without this correction,
   // six uniform height samples lose about 15% of the dense lowest layer,
   // creating a resolution-dependent step where the disk meets the limb.
   float exactColumn=max(0.,giantColumn(d*end-center,-d)-giantColumn(d*start-center,-d));
   float correction=exactColumn/max(columnSum,1.e-12);
   for(int j=0;j<steps;++j){
     vec3 p=d*(start+(float(j)+.5)*ds)-center;
     vec3 segmentT=exp(-beta*densities[j]*correction);
     vec3 world=(p+center)*uBody.w;
     vec3 sunlight=giantBeam(p);
     if(any(greaterThan(sunlight,vec3(0.)))){
       float shadow=sphereSunVisibility(world,uOccluderCount,uOccluders)*
                    ringSunTransmissionFiltered(world,uSun,0.);
       scatter+=transmit*(1.-segmentT)*phase*sunlight*shadow;
     }
     transmit*=segmentT;
   }
 }
 radiance=scatter;
 if(cloud){
   vec3 surface=d*end-center;
#ifdef GIANT_LIMB
   // This shader also covers inner subpixel rays: all used the path integral,
   // even below the nominal limb threshold. Never leave their cloud beam zero.
   cloudBeam=giantBeam(surface);
#else
   if(impact>=.999)cloudBeam=giantBeam(surface);
#endif
   radiance+=transmit*cloudBeam*cloudLight;
   opacity=1.;
 }else{
   // Fixed-function blending has a scalar alpha. Off-disk extinction uses
   // mean RGB transmission; on-disk extinction/scattering remain spectral.
   opacity=1.-dot(transmit,vec3(1./3.));
 }
}
