#version 300 es
precision highp float;
in vec3 vPosition,vNormal,vProperties,vMesh;
out vec4 color;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "clouds.glsl"
uniform highp sampler2D uScene;
uniform sampler2D uMaterial,uCanopy;
uniform highp sampler2DShadow uShadowNear,uShadowFar;
uniform mat4 uShadowMatrix[2];
uniform vec2 uMaterialOffset,uWaveOffset,uCanopyOffset;
uniform float uSceneScale;
uniform vec3 uPlanetDirection;
in vec2 vMaterial;
float shadowAt(highp sampler2DShadow tex,vec3 p,float bias){
 vec3 q=p*.5+.5;
 vec3 qx=dFdx(q),qy=dFdy(q);float determinant=qx.x*qy.y-qy.x*qx.y;
 vec2 gradient=abs(determinant)>1.e-16?vec2(qx.z*qy.y-qy.z*qx.y,qx.x*qy.z-qy.x*qx.z)/determinant:vec2(0.);
 // Receiver-plane bias covers the PCF/bilinear footprint. A constant depth
 // offset alone draws the radial mesh as contour-like self-shadow stripes.
 bias=max(bias,dot(abs(gradient),vec2(1./2048.))+.000003);
 if(any(lessThan(q,vec3(0.)))||any(greaterThan(q,vec3(1.))))return 1.;
 vec2 dx=vec2(.5/2048.,0.);float z=q.z-bias;
 return .25*(texture(tex,vec3(q.xy+dx+dx.yx,z))+texture(tex,vec3(q.xy+dx-dx.yx,z))+
              texture(tex,vec3(q.xy-dx+dx.yx,z))+texture(tex,vec3(q.xy-dx-dx.yx,z)));
}
float surfaceShadow(vec3 N){
 if(uSun.y<=0.)return 0.;
 vec3 a=(uShadowMatrix[0]*vec4(vMesh,1.)).xyz;
 vec3 b=(uShadowMatrix[1]*vec4(vMesh,1.)).xyz;
 float fine=shadowAt(uShadowNear,a,.00007+.00012*(1.-max(0.,dot(N,uSun))));
 float coarse=shadowAt(uShadowFar,b,.000035+.00007*(1.-max(0.,dot(N,uSun))));
 return mix(fine,coarse,smoothstep(.72,.95,max(abs(a.x),abs(a.y))));
}
void main(){
 vec3 d=normalize(vPosition);float distance=length(vPosition);
 vec3 N=normalize(vNormal);
 vec2 world=vMaterial+uMaterialOffset;
 vec4 detail=texture(uMaterial,world/24.);
 vec4 broad=texture(uMaterial,world/310.);
 // Meter-scale stochastic variation is spatially anchored and mip-filtered.
 float slope=1.-N.y,moisture=vProperties.y,temp=vProperties.z;
 float rock=smoothstep(.18,.48,slope);
 vec3 stone=mix(vec3(.15,.145,.13),vec3(.24,.23,.215),broad.b)*mix(.80,1.14,detail.g);
 vec3 meadow=mix(vec3(.075,.10,.028),vec3(.016,.039,.012),moisture)*mix(.77,1.18,detail.r);
 vec3 sand=mix(vec3(.27,.205,.125),vec3(.34,.29,.21),detail.b)*mix(.85,1.05,broad.r);
 float dry=1.-smoothstep(.30,.52,moisture);
 float cold=1.-smoothstep(-9.,3.,temp);
 vec3 tundra=mix(vec3(.10,.085,.055),vec3(.08,.115,.062),detail.g);
 vec3 albedo=mix(meadow,sand,dry*smoothstep(5.,17.,temp));
 float forest=smoothstep(.35,.63,moisture)*smoothstep(-5.,8.,temp)*(1.-smoothstep(1800.,2600.,vProperties.x));
 forest*=1.-smoothstep(.18,.40,slope);
 vec4 crown=texture(uCanopy,(vMaterial+uCanopyOffset)/512.);
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
 vec3 direct=transmission(uTrans,uObserverHeight,uSun.y)*max(dot(N,uSun),0.)*surfaceShadow(N)*cloudTransmission(vPosition/1000.)*uEclipse*.24;
 vec3 ambient=(sky(vec3(0,1,0))*.40+sky(normalize(N+vec3(0,.4,0)))*.60)*2.2;
 // Planet light uses the same irradiance-to-radiance scale as sunlight.
 // Its extended, nearly white disk gently lights the canopy at night.
 vec3 planet=vec3(.97,.98,1.)*uPlanetLight*.24*max(.05,dot(N,uPlanetDirection));
 ambient*=mix(1.,crown.a,forest);
 vec3 land=albedo*.72*(direct+ambient+planet+vec3(.00004)/uDarkGain);
 float water=(1.-smoothstep(-.25,.65,vProperties.x))*smoothstep(-5.,-1.,temp);
 vec2 phase=vec2(vMaterial.x*.17+vMaterial.y*.23*.4,vMaterial.y*.23-vMaterial.x*.17*.35),foot=fwidth(phase);
 vec2 amplitude=exp(-foot*foot*.5)*vec2(.003,.002);
 if(water>0.){
  vec3 wn=normalize(normalize(vNormal)+vec3(sin(mod(phase.x,2.*PI)+uWaveOffset.x)*amplitude.x,0.,sin(mod(phase.y,2.*PI)+uWaveOffset.y)*amplitude.y));
  vec3 reflected=reflect(d,wn);float z=dot(reflected,uForward);vec3 L=sky(reflected);
  if(z>0.){vec2 p=.5+.5*vec2(dot(reflected,uRight)/(z*uTanFov*uResolution.x/uResolution.y),dot(reflected,uUp)/(z*uTanFov));
   if(all(greaterThanEqual(p,vec2(0)))&&all(lessThanEqual(p,vec2(1))))L=texture(uScene,p).rgb/uSceneScale;}
  float fresnel=.0204+.9796*pow(1.-max(0.,dot(-d,wn)),5.);
  float angleSquared=dot(reflected-uSun,reflected-uSun);
  L+=transmission(uTrans,uObserverHeight,uSun.y)*uEclipse*smoothstep(-.006,.008,uSun.y)*.38*exp(-.5*angleSquared/(.009*.009));
  vec3 body=vec3(.003,.013,.016)*(sky(vec3(0,1,0))*.8+vec3(.00003)/uDarkGain);
  land=mix(land,L*fresnel+body*(1.-fresnel),water);
 }
 float meanHeight=max(0.,uObserverHeight+.0005*vPosition.y+dot(vPosition.xz,vPosition.xz)/(4.*6371000000.));
 vec3 extinction=betaR*exp(-meanHeight/8.)+betaM*1.11*exp(-meanHeight/1.2);
 vec3 T=exp(-distance*.001*extinction);
 land=land*T+sky(d)*(1.-T);
 color=vec4(min(land*uSceneScale,vec3(60000.)),1.);
}
