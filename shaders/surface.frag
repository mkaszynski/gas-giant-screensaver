#version 300 es
precision highp float;
in vec3 vPosition,vNormal,vProperties,vMesh;
out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "clouds.glsl"
uniform highp sampler2D uScene;
uniform sampler2D uMaterial,uCanopy,uReliefMaterial;
uniform highp sampler2DShadow uShadowNear,uShadowFar,uShadowNearNext,uShadowFarNext;
uniform mat4 uShadowMatrix[4];
uniform ivec4 uShadowSlots;
uniform vec2 uShadowBlend;
uniform vec2 uMaterialOffset,uWaveOffset,uCanopyOffset;
uniform vec2 uReliefOffset;
uniform float uReliefSpan,uReliefLongitudeScale;
uniform mat3 uMeshFrame;
uniform float uSceneScale;
uniform vec3 uPlanetDirection;
uniform bool uTerrainShadows;
in vec2 vMaterial;
in vec2 vRelief;
in float vUplift;
vec3 oceanRadiance(vec3 d,vec3 normal,vec2 wave,float amplitudeX,float amplitudeY){
 vec3 wn=normalize(normal+vec3(sin(mod(wave.x,2.*PI)+uWaveOffset.x)*amplitudeX,0.,sin(mod(wave.y,2.*PI)+uWaveOffset.y)*amplitudeY));
 vec3 reflected=reflect(d,wn);float z=dot(reflected,uForward);vec3 L=sky(reflected);
 if(z>0.){vec2 p=.5+.5*vec2(dot(reflected,uRight)/(z*uTanFov*uResolution.x/uResolution.y),dot(reflected,uUp)/(z*uTanFov));
  if(all(greaterThanEqual(p,vec2(0)))&&all(lessThanEqual(p,vec2(1))))L=texture(uScene,p).rgb/uSceneScale;}
 float angleSquared=dot(reflected-uSun,reflected-uSun);
 L+=transmission(uTrans,0.,uSun.y)*uEclipse*smoothstep(-.006,.008,uSun.y)*.38*exp(-.5*angleSquared/(.009*.009));
 L=cloudSceneAt(reflected,L,vPosition/1000.);
 float fresnel=.0204+.9796*pow(1.-max(0.,dot(-d,wn)),5.);
 vec3 body=vec3(.003,.013,.016)*(sky(vec3(0,1,0))*.8+vec3(.00003)/uDarkGain);
 return L*fresnel+body*(1.-fresnel);
}
vec3 aerialTransmission(float distance){
 float meanHeight=max(0.,uObserverHeight+.0005*vPosition.y+dot(vPosition.xz,vPosition.xz)/(4.*6371000000.));
 return exp(-distance*.001*(betaR*exp(-meanHeight/8.)+betaM*1.11*exp(-meanHeight/1.2)));
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
void main(){
 vec3 d=normalize(vPosition);float distance=length(vPosition);
 float water=(1.-smoothstep(-.25,.65,vProperties.x))*smoothstep(-5.,-1.,vProperties.z);
 vec2 phase=vec2(vMaterial.x*.17+vMaterial.y*.23*.4,vMaterial.y*.23-vMaterial.x*.17*.35),foot=fwidth(phase);
 vec2 amplitude=exp(-foot*foot*.5)*vec2(.003,.002);
 // Sphere normal avoids coastal land slopes tilting the sea reflection.
 vec3 waterNormal=normalize(vec3(vPosition.x/6371000.,1.,vPosition.z/6371000.));
 // Pure water avoids land's shadow/normal/material work. Mixed shore pixels
 // use explicit gradients below so divergent branches cannot alias textures.
 vec2 material=(vMaterial+uMaterialOffset)/24.,broadUV=(vMaterial+uMaterialOffset)/310.;
 vec2 reliefUV=(vRelief+uReliefOffset)/uReliefSpan,canopyUV=(vMaterial+uCanopyOffset)/512.;
 vec2 mdx=dFdx(material),mdy=dFdy(material),bdx=dFdx(broadUV),bdy=dFdy(broadUV);
 vec2 rdx=dFdx(reliefUV),rdy=dFdy(reliefUV),cdx=dFdx(canopyUV),cdy=dFdy(canopyUV);
 vec3 meshDx=dFdx(vMesh),meshDy=dFdy(vMesh);
 if(water>=1.){
  vec3 L=oceanRadiance(d,waterNormal,phase,amplitude.x,amplitude.y),T=aerialTransmission(distance);
  color=vec4(min((L*T+sky(d)*(1.-T))*uSceneScale,vec3(60000.)),1.);return;
 }
 vec3 N=normalize(vNormal);
 vec4 landform=textureGrad(uReliefMaterial,reliefUV,rdx,rdy);
 float reliefStrength=(.65+.75*vUplift)*smoothstep(2.,120.,vProperties.x);
 vec2 residual=landform.rg*2.-1.;
 float vertical=sqrt(max(.03,1.-dot(residual,residual)));
 N=normalize(N/max(.05,N.y)+uMeshFrame*vec3(residual.x*uReliefLongitudeScale,0,residual.y)*reliefStrength/vertical);
 vec2 world=vMaterial+uMaterialOffset;
 vec4 detail=textureGrad(uMaterial,material,mdx,mdy);
 vec4 broad=textureGrad(uMaterial,broadUV,bdx,bdy);
 // Meter-scale stochastic variation is spatially anchored and mip-filtered.
 float slope=1.-N.y,moisture=vProperties.y,temp=vProperties.z;
 float rock=smoothstep(.18,.48,slope);
 vec3 stone=mix(vec3(.15,.145,.13),vec3(.24,.23,.215),broad.b)*mix(.80,1.14,detail.g);
 vec3 meadow=mix(vec3(.075,.10,.028),vec3(.016,.039,.012),moisture)*mix(.77,1.18,detail.r);
 // Valleys retain moisture and sediment; exposed crests are drier. These
 // signals derive from actual relief, not a photograph or per-pixel noise.
 meadow*=mix(vec3(.76,.91,.75),vec3(1.16,1.06,.91),landform.b);
 vec3 sand=mix(vec3(.27,.205,.125),vec3(.34,.29,.21),detail.b)*mix(.85,1.05,broad.r);
 float dry=1.-smoothstep(.30,.52,moisture);
 float cold=1.-smoothstep(-9.,3.,temp);
 vec3 tundra=mix(vec3(.10,.085,.055),vec3(.08,.115,.062),detail.g);
 vec3 albedo=mix(meadow,sand,dry*smoothstep(5.,17.,temp));
 float forest=smoothstep(.35,.63,moisture)*smoothstep(-5.,8.,temp)*(1.-smoothstep(1800.,2600.,vProperties.x));
 forest*=1.-smoothstep(.18,.40,slope);
 vec4 crown=textureGrad(uCanopy,canopyUV,cdx,cdy);
 vec3 woodland=mix(vec3(.020,.052,.013),vec3(.040,.082,.023),broad.g)*mix(.8,1.07,crown.b);
 albedo=mix(albedo,woodland,forest);
 albedo=mix(albedo,tundra,cold*(1.-rock));albedo=mix(albedo,stone,rock);
 float snow=(1.-smoothstep(-7.,1.,temp))*smoothstep(.3,.8,N.y);
 albedo=mix(albedo,vec3(.72,.76,.80)*mix(.89,1.01,detail.g),snow);
 // Fine relief perturbs lighting without displacement/noise at distant scales.
 vec2 grain=(detail.rg-.5)*.065*(1.-snow*.6);
 vec2 crownSlope=crown.rg*2.-1.;
 vec3 crownNormal=normalize(vec3(crownSlope.x,sqrt(max(.03,1.-dot(crownSlope,crownSlope))),crownSlope.y));
 N=normalize(N+vec3(grain.x,0.,grain.y)+forest*(crownNormal-vec3(0,1,0))*.65);
 vec3 direct=transmission(uTrans,uObserverHeight,uSun.y)*max(dot(N,uSun),0.)*surfaceShadow(N,meshDx,meshDy)*cloudTransmission(vPosition/1000.)*uEclipse*.24;
 vec3 ambient=(sky(vec3(0,1,0))*.40+sky(normalize(N+vec3(0,.4,0)))*.60)*2.2;
 // Planet light uses the same irradiance-to-radiance scale as sunlight.
 // Its extended, nearly white disk gently lights the canopy at night.
 vec3 planet=vec3(.97,.98,1.)*uPlanetLight*.24*max(.05,dot(N,uPlanetDirection));
 ambient*=mix(1.,crown.a,forest)*mix(1.,landform.a,min(1.,reliefStrength));
 vec3 land=albedo*.72*(direct+ambient+planet+vec3(.00004)/uDarkGain);
 if(water>0.)land=mix(land,oceanRadiance(d,waterNormal,phase,amplitude.x,amplitude.y),water);
 vec3 T=aerialTransmission(distance);
 land=land*T+sky(d)*(1.-T);
 color=vec4(min(land*uSceneScale,vec3(60000.)),1.);
}
