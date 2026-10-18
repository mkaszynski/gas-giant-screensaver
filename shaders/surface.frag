#version 300 es
precision highp float;
in vec3 vPosition,vNormal,vProperties,vMesh;
layout(location=0)out vec4 color;
#if !defined(OBSERVATORY_WEATHER_MODE) || OBSERVATORY_WEATHER_MODE
layout(location=1)out float weatherDistance;
#endif
#include "atmosphere.glsl"
#include "scene.glsl"
#include "clouds.glsl"
uniform highp sampler2D uScene,uLandReflection;
uniform bool uLandReflectionEnabled;
uniform sampler2D uMaterial,uCanopy;
uniform highp sampler2DArray uReliefMaterial;
in vec2 vLandforms;
uniform highp sampler2DArray uPhysicalAlbedo,uPhysicalNormal,uCrownShadows;
uniform highp sampler2DShadow uShadowNear,uShadowFar,uShadowNearNext,uShadowFarNext;
uniform mat4 uShadowMatrix[4];
uniform ivec4 uShadowSlots;
uniform vec2 uShadowBlend;
uniform vec2 uMaterialOffset,uCanopyOffset;
uniform vec4 uWaves[4];
uniform vec2 uReliefOffset;
uniform float uReliefSpan,uReliefLongitudeScale,uGeologyPhase;
uniform mat3 uMeshFrame;
uniform float uSceneScale;
uniform vec3 uPlanetDirection;
uniform bool uTerrainShadows;
in vec2 vMaterial;
in vec2 vRelief;
in float vReliefStrength;
float frameConfidence(vec2 p){return smoothstep(0.,.04,min(min(p.x,1.-p.x),min(p.y,1.-p.y)));}
float dielectricSpecular(vec3 N,vec3 V,vec3 L,float roughness,float f0);
vec3 oceanRadiance(vec3 d,vec3 normal,vec4 phase,vec4 amplitude){
 vec2 slope=vec2(0.);float unresolved=0.;
 for(int i=0;i<4;++i){
  slope+=normalize(uWaves[i].xy)*sin(mod(phase[i],2.*PI)+uWaves[i].z)*amplitude[i];
  float wind=uWeatherEnabled?1.+.7*uWeatherKind.y:1.;
  unresolved+=(uWaves[i].w*uWaves[i].w*wind*wind-amplitude[i]*amplitude[i])*.5;
 }
 vec3 wn=normalize(normal+uMeshFrame*vec3(slope.x*uReliefLongitudeScale,0.,slope.y));
 vec3 reflected=reflect(d,wn);float z=dot(reflected,uForward);vec3 L=sky(reflected);
 if(z>0.){vec2 p=.5+.5*vec2(dot(reflected,uRight)/(z*uTanFov*uResolution.x/uResolution.y),dot(reflected,uUp)/(z*uTanFov));
  if(all(greaterThanEqual(p,vec2(0)))&&all(lessThanEqual(p,vec2(1))))L=mix(L,texture(uScene,p).rgb/uSceneScale,frameConfidence(p));}
 // Solar energy is integrated by the filtered BRDF below. Remove the sharp
 // background solar disk from this sample to avoid counting it twice.
 if(dot(reflected,uSun)>cos(.006))L=sky(reflected);
 L=cloudSceneAt(reflected,L,vPosition/1000.);
 if(uLandReflectionEnabled){
  vec3 rf=vec3(uForward.x,-uForward.y,uForward.z),rr=vec3(uRight.x,-uRight.y,uRight.z),ru=vec3(uUp.x,-uUp.y,uUp.z);
  float rz=dot(reflected,rf);
  if(rz>0.){
   vec2 p=.5+.5*vec2(dot(reflected,rr)/(rz*uTanFov*1.5*uResolution.x/uResolution.y),dot(reflected,ru)/(rz*uTanFov*1.5));
   if(all(greaterThanEqual(p,vec2(0)))&&all(lessThanEqual(p,vec2(1)))){
    vec4 ground=texture(uLandReflection,p)*frameConfidence(p);L=L*(1.-ground.a)+ground.rgb/uSceneScale;
   }
  }
 }
 float fresnel=.0204+.9796*pow(1.-max(0.,dot(-d,wn)),5.);
 float depth=max(0.,-vProperties.x);
 vec3 attenuation=exp(-vec3(.45,.14,.08)*depth);
 vec3 seaSolar=transmission(uTrans,0.,uSun.y)*uEclipse;
 vec3 illumination=sky(vec3(0,1,0))+seaSolar*max(0.,uSun.y)/PI+vec3(.00003)/uDarkGain;
 vec3 body=mix(vec3(.003,.013,.016),vec3(.18,.145,.09),attenuation)*illumination;
 float roughness=max(.09,pow(max(0.,unresolved),.25));
 float spec=dielectricSpecular(wn,-d,uSun,roughness,.0204);
 vec3 glint=seaSolar*max(dot(wn,uSun),0.)*spec;
 // Attenuating imperceptible highlights would spend five cloud lookups on
 // nearly every water pixel. Keep the BRDF tail, skip only its tiny shadow term.
 if(max(max(glint.r,glint.g),glint.b)*uExposure>.0001)glint*=cloudTransmission(vPosition/1000.);
 return L*fresnel+body*(1.-fresnel)+glint;
}
vec3 aerialTransmission(float distance){
 float meanHeight=max(0.,uObserverHeight+.0005*vPosition.y+dot(vPosition.xz,vPosition.xz)/(4.*6371000000.));
 #ifdef SURFACE_REFLECTION
 // Mirror geometry lies below the local sea plane; the real light path stays
 // above it. Integrate the absolute-height segments instead of negative air.
 float groundHeight=max(0.,vPosition.y*.001-uObserverHeight+dot(vPosition.xz,vPosition.xz)/(2.*6371000000.));
 meanHeight=(uObserverHeight*uObserverHeight+groundHeight*groundHeight)/(2.*max(.001,uObserverHeight+groundHeight));
 #endif
 return exp(-distance*.001*(betaR*exp(-meanHeight/8.)+betaM*1.11*exp(-meanHeight/1.2)));
}
float terrainCloudTransmission(vec3 point){
 #ifdef SURFACE_REFLECTION
 return cloudTransmissionAt(point,-uObserverHeight);
 #else
 return cloudTransmission(point);
 #endif
}
float shadowAt(highp sampler2DShadow tex,vec3 p,float bias,vec3 qx,vec3 qy){
 vec3 q=p*.5+.5;
 float determinant=qx.x*qy.y-qy.x*qx.y;
 vec2 gradient=abs(determinant)>1.e-16?vec2(qx.z*qy.y-qy.z*qx.y,qx.x*qy.z-qy.x*qx.z)/determinant:vec2(0.);
 // Receiver-plane bias covers the PCF/bilinear footprint. A constant depth
 // offset alone draws the radial mesh as contour-like self-shadow stripes.
 bias=max(bias,dot(abs(gradient),vec2(1./2048.))+.000003);
 if(any(lessThan(q,vec3(0.)))||any(greaterThan(q,vec3(1.))))return 1.;
 vec2 dx=vec2(.5/2048.,0.);float z=q.z-bias;
 return .25*(texture(tex,vec3(q.xy+dx+dx.yx,z))+texture(tex,vec3(q.xy+dx-dx.yx,z))+
              texture(tex,vec3(q.xy-dx+dx.yx,z))+texture(tex,vec3(q.xy-dx-dx.yx,z)));
}
float temporalShadow(highp sampler2DShadow a,highp sampler2DShadow b,
                     int previous,int next,float weight,float bias,vec3 dx,vec3 dy){
 mat4 ma=uShadowMatrix[previous],mb=uShadowMatrix[next];
 float before=shadowAt(a,(ma*vec4(vMesh,1.)).xyz,bias,.5*(ma*vec4(dx,0.)).xyz,.5*(ma*vec4(dy,0.)).xyz);
 float after=shadowAt(b,(mb*vec4(vMesh,1.)).xyz,bias,.5*(mb*vec4(dx,0.)).xyz,.5*(mb*vec4(dy,0.)).xyz);
 return mix(before,after,weight);
}
float surfaceShadow(vec3 N,vec3 dx,vec3 dy){
 if(uSun.y<=0.)return 0.;
 if(!uTerrainShadows)return 1.;
 // A continuous distance-based cascade selection does not jump when a cache
 // projection is recycled. Usually only one cascade is needed: two temporal
 // maps cost the same eight PCF taps as the old near+far pair.
 float coarseWeight=smoothstep(.65,.85,length(vPosition)/8000.);
 float slope=1.-max(0.,dot(N,uSun));
 float fine=1.,coarse=1.;
 if(coarseWeight<1.)fine=temporalShadow(uShadowNear,uShadowNearNext,uShadowSlots.x,uShadowSlots.y,uShadowBlend.x,.00007+.00012*slope,dx,dy);
 if(coarseWeight>0.)coarse=temporalShadow(uShadowFar,uShadowFarNext,uShadowSlots.z,uShadowSlots.w,uShadowBlend.y,.000035+.00007*slope,dx,dy);
 return mix(fine,coarse,coarseWeight);
}
// Dielectric GGX, energy-conserving Fresnel and correlated Smith visibility.
// Roughness includes normal variation so distant highlights do not shimmer.
float dielectricSpecular(vec3 N,vec3 V,vec3 L,float roughness,float f0){
 if(dot(N,V)<=0.||dot(N,L)<=0.)return 0.;
 vec3 H=normalize(V+L);float NoV=max(.001,dot(N,V)),NoL=max(.001,dot(N,L));
 float NoH=max(0.,dot(N,H)),VoH=max(0.,dot(V,H));
 float alpha=roughness*roughness,a2=alpha*alpha;
 float denominator=NoH*NoH*(a2-1.)+1.;
 float D=a2/(PI*denominator*denominator);
 float visibility=.5/(NoL*sqrt(NoV*NoV*(1.-a2)+a2)+NoV*sqrt(NoL*NoL*(1.-a2)+a2));
 return D*visibility*(f0+(1.-f0)*pow(1.-VoH,5.));
}
void physicalMaterial(vec2 uv,vec2 dx,vec2 dy,float dry,float cold,float rock,float snow,float soil,
                      out vec3 albedo,out vec3 normal,out float roughness,out float cavity){
 float weights[6];weights[0]=rock*(1.-snow);
 weights[1]=(1.-rock)*(1.-snow)*(1.-soil)*(1.-dry)*(1.-cold);
 weights[2]=(1.-rock)*(1.-snow)*(1.-soil)*dry*(1.-cold);
 weights[3]=(1.-rock)*(1.-snow)*(1.-soil)*cold;weights[4]=snow;
 weights[5]=(1.-rock)*(1.-snow)*soil;
 albedo=vec3(0.);normal=vec3(0.);roughness=0.;cavity=0.;
 for(int layer=0;layer<6;++layer){
  float w=weights[layer];if(w<=.0001)continue;
  vec4 a=textureGrad(uPhysicalAlbedo,vec3(uv,float(layer)),dx,dy);
  vec4 n=textureGrad(uPhysicalNormal,vec3(uv,float(layer)),dx,dy);
  albedo+=w*a.rgb;normal+=w*vec3(n.x*2.-1.,n.a,n.y*2.-1.);
  roughness+=w*a.a;cavity+=w*n.b;
 }
}
vec4 wavePhase(){return vec4(dot(vMaterial,uWaves[0].xy),dot(vMaterial,uWaves[1].xy),dot(vMaterial,uWaves[2].xy),dot(vMaterial,uWaves[3].xy));}
vec3 preparedOcean(vec3 d,vec4 foot){
 vec4 phase=wavePhase();
 vec4 amplitude=exp(-foot*foot*.5)*vec4(uWaves[0].w,uWaves[1].w,uWaves[2].w,uWaves[3].w);
 if(uWeatherEnabled)amplitude*=1.+.7*uWeatherKind.y;
 // Sphere normal avoids coastal land slopes tilting the sea reflection.
 vec3 waterNormal=normalize(vec3(vPosition.x/6371000.,1.,vPosition.z/6371000.));
 return oceanRadiance(d,waterNormal,phase,amplitude);
}
void main(){
 vec3 d=normalize(vPosition);float distance=length(vPosition);
#if !defined(OBSERVATORY_WEATHER_MODE) || OBSERVATORY_WEATHER_MODE
 weatherDistance=distance*.001;
#endif
 #ifdef SURFACE_DEEP_WATER
 // Selected only for blocks whose vertices are all below -64 m and warm
 // enough to be liquid. Linear interpolation preserves those bounds, so the
 // general shader's water mask is exactly one over every such triangle.
 const float water=1.;
 #else
 float water=(1.-smoothstep(-.25,.65,vProperties.x))*smoothstep(-5.,-1.,vProperties.z);
 #endif
 #ifdef SURFACE_PURE_LAND
 // All mesh vertices are above the shoreline blend, not only this block.
 // Keep the early water branch for helper lanes and remove the final blend
 // after land derivatives. Mixed coastal meshes retain the general shader.
 const float shorelineBlend=0.;
 #else
 float shorelineBlend=water;
 #endif
 // Derivatives stay outside divergent shoreline branches. Prepare the other
 // water values only at their use, without keeping them live across land PBR.
 vec4 foot=fwidth(wavePhase());
 // Pure water avoids land's shadow/normal/material work. Mixed shore pixels
 // use explicit gradients below so divergent branches cannot alias textures.
 vec2 material=(vMaterial+uMaterialOffset)/24.,broadUV=(vMaterial+uMaterialOffset)/310.;
 vec2 reliefUV=(vRelief+uReliefOffset)/uReliefSpan,canopyUV=(vMaterial+uCanopyOffset)/512.;
 vec2 mdx=dFdx(material),mdy=dFdy(material),bdx=dFdx(broadUV),bdy=dFdy(broadUV);
 vec2 rdx=dFdx(reliefUV),rdy=dFdy(reliefUV),cdx=dFdx(canopyUV),cdy=dFdy(canopyUV);
 vec3 meshDx=dFdx(vMesh),meshDy=dFdy(vMesh);
 #ifdef SURFACE_REFLECTION
 if(water>.001)discard;
 #endif
 if(water>=1.){
  vec3 L=preparedOcean(d,foot),T=aerialTransmission(distance);
  color=vec4(min((L*T+sky(d)*(1.-T))*uSceneScale,vec3(60000.)),1.);return;
 }
 vec3 N=normalize(vNormal);
 vec3 weights=vec3(max(0.,1.-vLandforms.x-vLandforms.y),vLandforms);
 vec4 landform=vec4(0.);
 vec2 reliefSlope=vec2(0.);
 for(int i=0;i<3;++i){if(weights[i]>.0001){
  vec4 field=textureGrad(uReliefMaterial,vec3(reliefUV,float(i)),rdx,rdy);
  vec2 encoded=field.rg*2.-1.;
  reliefSlope+=weights[i]*encoded/sqrt(max(.03,1.-dot(encoded,encoded)));
  landform+=weights[i]*field;
 }}
 float reliefStrength=vReliefStrength;
 // Vertex normals contain continental/canopy curvature only. The full DEM
 // gradient restores small gullies without double-counting geometry slopes.
 vec2 correction=reliefSlope*vec2(uReliefLongitudeScale,1.)*reliefStrength;
 N=normalize(N/max(.05,N.y)+uMeshFrame*vec3(correction.x,0.,correction.y));


 vec4 broad=textureGrad(uMaterial,broadUV,bdx,bdy);
 // Meter-scale stochastic variation is spatially anchored and mip-filtered.
 float slope=1.-N.y,moisture=vProperties.y,temp=vProperties.z;
 float rock=smoothstep(.18,.48,slope);
 float dry=(1.-smoothstep(.30,.52,moisture))*smoothstep(5.,17.,temp);
 float cold=1.-smoothstep(-9.,3.,temp);
 float snow=(1.-smoothstep(-7.,1.,temp))*smoothstep(.3,.8,N.y);
 vec3 albedo,materialNormal;float roughness,cavity;
 float soil=smoothstep(.52,.76,moisture)*(1.-smoothstep(.35,.55,landform.b));
 physicalMaterial(material,mdx,mdy,dry,cold,rock,snow,soil,albedo,materialNormal,roughness,cavity);
 albedo*=mix(.84,1.12,broad.r);
 // Regional mineral tint and tilted sediment beds follow world height. Their
 // footprint filters the bands instead of turning distant cliffs into stripes.
 float beddingPhase=(vProperties.x+vMaterial.x*.03+vMaterial.y*.015)*(2.*PI/50.)+uGeologyPhase;
 float beddingFoot=fwidth(beddingPhase);
 float bedding=sin(beddingPhase)*exp(-.5*beddingFoot*beddingFoot);
 albedo*=mix(vec3(1.),vec3(1.32,.79,.52)*(1.+bedding*.10),rock*vLandforms.x);
 albedo*=mix(vec3(.82,.94,.81),vec3(1.10,1.05,.96),landform.b)*(1.-snow)+vec3(snow);
 float forest=smoothstep(.35,.63,moisture)*smoothstep(-5.,8.,temp)*(1.-smoothstep(1800.,2600.,vProperties.x));
 forest*=1.-smoothstep(.18,.40,slope);
 vec4 crown=textureGrad(uCanopy,canopyUV,cdx,cdy);
 vec3 woodland=mix(vec3(.015,.039,.010),vec3(.033,.072,.020),broad.g)*mix(.72,1.13,crown.b);
 forest*=1.-snow;
 albedo=mix(albedo,woodland,forest);
 // Tangent fields follow the same geographic frame as their anchored UVs.
 vec3 detailSlope=uMeshFrame*vec3(materialNormal.x*uReliefLongitudeScale,0.,materialNormal.z)/max(.10,materialNormal.y);
 vec2 crownSlope=crown.rg*2.-1.;
 vec3 crownNormal=vec3(crownSlope.x,sqrt(max(.03,1.-dot(crownSlope,crownSlope))),crownSlope.y);
 N=normalize(N+detailSlope*(1.-forest)+uMeshFrame*vec3(crownNormal.x*uReliefLongitudeScale,0.,crownNormal.z)/max(.10,crownNormal.y)*forest);
 // Directional crown visibility is baked before filtering, retaining small
 // tree shadows when individual crowns become smaller than a screen pixel.
 vec3 meshSun=transpose(uMeshFrame)*uSun;
 float canopyShade=1.;
 if(forest>.001&&uSun.y>0.){
 vec3 crownRay=vec3(meshSun.x*uReliefLongitudeScale,meshSun.y,meshSun.z);
 float sector=mod(atan(crownRay.z,crownRay.x)/(2.*PI)*8.+8.,8.);
 vec4 before=textureGrad(uCrownShadows,vec3(canopyUV,floor(sector)),cdx,cdy);
 vec4 after=textureGrad(uCrownShadows,vec3(canopyUV,mod(floor(sector)+1.,8.)),cdx,cdy);
 vec4 visibilityLevels=mix(before,after,fract(sector));
 float solarElevation=atan(max(0.,dot(uSun,normalize(vNormal))),max(.001,length(crownRay.xz)))*180./PI;
 canopyShade=solarElevation<25.?mix(visibilityLevels.r,visibilityLevels.g,clamp((solarElevation-5.)/20.,0.,1.)):
                   solarElevation<55.?mix(visibilityLevels.g,visibilityLevels.b,(solarElevation-25.)/30.):
                   mix(visibilityLevels.b,visibilityLevels.a,clamp((solarElevation-55.)/30.,0.,1.));
 }
 float visibility=surfaceShadow(N,meshDx,meshDy)*terrainCloudTransmission(vPosition/1000.)*uEclipse;
 visibility*=mix(1.,canopyShade,forest);
 vec3 irradiance=transmission(uTrans,uObserverHeight,uSun.y)*visibility;
 float NoL=max(dot(N,uSun),0.);
 vec3 ambient=sky(vec3(0,1,0))*.35+sky(normalize(N+vec3(0,.4,0)))*.65;
 ambient*=mix(cavity,crown.a,forest)*mix(1.,landform.a,min(1.,reliefStrength));
 vec3 planet=vec3(.97,.98,1.)*uPlanetLight/PI*max(.05,dot(N,uPlanetDirection));
 vec3 land=albedo*.96*(irradiance*NoL/PI+ambient+planet+vec3(.00004)/uDarkGain);
 // Thin leaf transmission is strongest towards the sun, and cannot emit light
 // at night or through an eclipse. It avoids flat uniformly dark canopies.
 float leafBack=pow(max(dot(d,uSun),0.),6.)*max(0.,meshSun.y)*forest;
 land+=albedo*irradiance*leafBack*.12;
 float variance=min(.25,dot(dFdx(N),dFdx(N))+dot(dFdy(N),dFdy(N)));
 roughness=sqrt(min(1.,roughness*roughness+variance));
 float wetness=(1.-smoothstep(.4,6.,vProperties.x))*(1.-cold);
 if(uWeatherEnabled)wetness=max(wetness,uWeatherRain*.65*(1.-cold));
 land*=mix(1.,.62,wetness);
 roughness=mix(roughness,.28,wetness);
 float spec=dielectricSpecular(N,-d,uSun,max(.28,roughness),.04);
 land+=irradiance*NoL*spec*mix(1.,.35,forest);
 if(shorelineBlend>0.)land=mix(land,preparedOcean(d,foot),shorelineBlend);
 vec3 T=aerialTransmission(distance);
 land=land*T+sky(d)*(1.-T);
 color=vec4(min(land*uSceneScale,vec3(60000.)),1.);
}
