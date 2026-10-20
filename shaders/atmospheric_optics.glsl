// Offline, spectrally integrated phase functions (inverse steradians).
// Ice side-face refraction and Mie water scattering are filtered by the real
// solar disk. Lookup coordinates address texel centres at every table edge.
uniform highp sampler2D uAtmosphericOptics;
uniform bool uAtmosphericOpticsEnabled;
uniform vec2 uOpticsLiquid; // lower cloud and fog liquid-water fractions
uniform vec2 uOpticsSun; // elevation in radians, cos(elevation)
vec3 opticsPhase(float angle,float row,bool water){
 float x=water?(angle-2.*PI/3.)/(PI/3.):angle/PI;
 float u=water?(clamp(x,0.,1.)*4095.+.5)/4096.:clamp(x,0.,1.);
 return textureLod(uAtmosphericOptics,vec2(u,(row+.5)/67.),0.).rgb;
}
vec3 cloudOpticsPhase(vec3 d,float original){
 if(!uAtmosphericOpticsEnabled||uOpticsLiquid.x<=0.||uSun.y<=-.08||uEclipse<=0.)return vec3(original);
 float cosine=clamp(dot(d,uSun),-1.,1.);
 if(cosine>=-.5)return vec3(original);
 float angle=acos(cosine);
 float blend=smoothstep(2.*PI/3.,25.*PI/36.,angle)*uOpticsLiquid.x;
 return mix(vec3(original),opticsPhase(angle,64.,true),blend);
}
vec3 iceOpticsPhase(vec3 d,float original){
 if(!uAtmosphericOpticsEnabled||uSun.y<=-.08||uEclipse<=0.)return vec3(original);
 float angle=acos(clamp(dot(d,uSun),-1.,1.));
 vec3 randomIce=opticsPhase(angle,0.,false),plates=vec3(0.);
 // Horizontal plate normals have a narrow continuous wobble distribution.
 // Parhelia move outward with solar elevation through the skew-ray table.
 const float sigma=.01416; // 0.80-degree wobble plus finite solar disk variance
 if(uOpticsSun.x<62.*PI/180.&&uOpticsSun.y>.001){
  float altitude=asin(clamp(d.y,-1.,1.));
  float delta=(altitude-uOpticsSun.x)/sigma;
  if(abs(delta)<5.){
   float horizontal=sqrt(max(1.e-8,dot(d.xz,d.xz)))*uOpticsSun.y;
   float azimuth=acos(clamp(dot(d.xz,uSun.xz)/horizontal,-1.,1.));
   float height=clamp(uOpticsSun.x*180./PI,0.,62.);
   plates=opticsPhase(azimuth,1.+height,false)*
          (exp(-.5*delta*delta)/(2.*sqrt(2.*PI)*sigma*max(.01,uOpticsSun.y)));
  }
 }
 // Only a minority of ice-cloud scattering follows well-formed 60-degree
 // side-face paths. The rest retains the existing broad cloud phase model.
 return mix(vec3(original),.8*randomIce+.2*plates,.15);
}
vec3 precipitationOptics(vec3 d,vec3 original,vec3 solar,float fog,float rain,
                         vec3 illuminatedPoint){
 if(!uAtmosphericOpticsEnabled||uOpticsLiquid.y<=0.||uSun.y<=.015||uEclipse<=0.||fog+rain<=1.e-6)return original;
 float cosine=clamp(dot(d,uSun),-1.,1.);
 if(cosine>=-.5)return original;
 float angle=acos(cosine),blend=smoothstep(2.*PI/3.,25.*PI/36.,angle);
 vec3 phase=(fog*opticsPhase(angle,65.,true)+rain*opticsPhase(angle,66.,true))/max(1.e-6,fog+rain);
 float sunlight=weatherShadowAt(illuminatedPoint,uObserverHeight)*smoothstep(.015,.05,uSun.y);
 // The old smooth term stands for diffuse illumination. Replace its direct
 // component in the singly scattered liquid fraction, retaining ambient light.
 return original+solar*(phase-vec3(.03*max(0.,uSun.y)))*blend*sunlight*uOpticsLiquid.y;
}
