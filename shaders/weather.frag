#version 300 es
precision highp float;
in vec2 uv;
layout(location=0)out vec4 color;
layout(location=1)out vec4 skyWeather;
uniform highp sampler2D uSurface;
#include "atmosphere.glsl"
#include "scene.glsl"
#include "weather_common.glsl"
#include "weather_depth.glsl"
uniform float uSceneScale;
uniform vec2 uWeatherResolution;
uniform highp sampler3D uWeatherLightCache;
vec2 cloudSunTransmission(vec3 p,float density,float lod){
 if(uSun.y<=-.08||uEclipse<=0.)return vec2(1.);
 float stepLength=clamp(uWeatherLayer.y*.25,.06,1.);
 float edge=smoothstep(12.,16.,max(abs(p.x),abs(p.z)));
 vec2 column=vec2(1.);
 if(edge<1.){
  vec3 cacheUV=vec3(p.xz/32.+.5,(weatherHeight(p,uObserverHeight)-uWeatherLayer.x)/uWeatherLayer.y);
  column=textureLod(uWeatherLightCache,cacheUV,0.).rg;
 }
 if(edge>0.){
  float distant=stepLength*(.9*weatherDensity(p+uSun*(stepLength*.5),uObserverHeight,lod+1.)+
                1.4*weatherDensity(p+uSun*(stepLength*1.5),uObserverHeight,lod+1.));
  column=mix(column,exp(-distant*vec2(1.,.2)),edge);
 }
 // Fine local extinction preserves the illuminated cloud skin even when the
 // wider solar column is filtered through the quarter-kilometre light cache.
 return column*exp(-stepLength*density*.35*vec2(1.,.2));
}

// No per-frame dither or stochastic reprojection: bounded, stable quadrature
// produces clean screenshots and avoids temporal trails on moons and ridges.
vec4 integrateWeather(vec3 d,float end){
 vec2 outer=weatherShell(vec3(0.),d,uWeatherLayer.x+uWeatherLayer.y,uObserverHeight);
 vec2 inner=weatherShell(vec3(0.),d,uWeatherLayer.x,uObserverHeight);
 float start=max(0.,outer.x),finish=min(end,outer.y);
 // Above the layer looking down: the bottom is the near inner intersection.
 if(uObserverHeight>uWeatherLayer.x&&inner.x>0.)finish=min(finish,inner.x);
 // Below the layer looking up: start on the far inner intersection.
 if(uObserverHeight<uWeatherLayer.x)start=max(start,inner.y);
 vec3 L=vec3(0.);float T=1.;
 vec3 viewSky=sky(d);
 // A short path does not need 48 intervals through the same smooth noise cells.
 // Bound the interval count to retain cloud edges and grazing-ray detail.
 float span=max(0.,finish-start),baseStep=span/clamp(span/.15,12.,48.);
 if(span>0.){
  WeatherLighting lightingTerms=weatherLighting(d);
  lightingTerms.ambient+=lightingTerms.planet;
  float position=start;
  for(int i=0;i<48;++i){
   if(position>=finish)break;
   // Preserve fine quadrature in transparent skins and silhouettes. Deep in
   // an opaque body, smoothly enlarge steps where little radiance can escape.
   float stepLength=min(finish-position,baseStep*(1.+2.*(1.-smoothstep(.02,.2,T))));
   float distance=position+stepLength*.5;
   position+=stepLength;
   vec3 p=d*distance;
   float footprint=max(.001,distance*uTanFov*2./uWeatherResolution.y);
   float lod=clamp(log2(footprint*(64./14.)),-6.,3.);
   float density=weatherDensity(p,uObserverHeight,lod)*(1.-smoothstep(75.,100.,distance));
   if(density>.001){
    float segmentT=exp(-density*stepLength);
    float extinction=1.-segmentT;
    // Two up-sun probes capture illuminated rims and dark storm cores.
    vec2 shadow=cloudSunTransmission(p,density,lod);
    // Combine scalar solar weights before the vector multiplication. The
    // surface path retains its original evaluation order; volume roundoff is
    // bounded by the rendered-reference regression test.
    vec3 solar=transmission(uTrans,weatherHeight(p,uObserverHeight),uSun.y)*lightingTerms.sunlit;
    vec3 lighting=solar*(lightingTerms.forward*shadow.x+.032*(.35+.65*shadow.y))+lightingTerms.ambient;
    if(uWeatherFlash.w>0.){
     vec3 delta=p-vec3(uWeatherFlash.x,uWeatherFlash.y-uObserverHeight,uWeatherFlash.z);
     lighting+=vec3(.90,.94,1.)*uWeatherFlash.w*exp(-dot(delta,delta)/18.);
    }
    float meanHeight=max(0.,.5*(uObserverHeight+weatherHeight(p,uObserverHeight)));
    vec3 air=exp(-distance*(betaR*exp(-meanHeight/8.)+betaM*1.11*exp(-meanHeight/1.2)));
    lighting=lighting*air+viewSky*(1.-air);
    L+=T*extinction*lighting;T*=segmentT;
    if(T<.0001)break;
   }
  }
 }
 // High thin ice clouds are a distinct layer, kept optically thin in fair
 // weather. The giant is usually visible between lower cloud banks.
 vec2 high=weatherShell(vec3(0.),d,9.5,uObserverHeight);
 float highDistance=high.x>0.?high.x:high.y;
 if(uWeatherKind.w>.001&&highDistance>0.&&highDistance<end){
  float tau=weatherCirrus(d*highDistance)/max(.2,abs(d.y))*(1.-smoothstep(70.,100.,highDistance));
  float a=1.-exp(-tau);
  vec3 light=weatherLight(d,9.5,.1);
  if(highDistance<start){L=light*a+L*(1.-a);T*=1.-a;}
  else {L+=T*light*a;T*=1.-a;}
 }
 // A shallow moisture-dependent aerosol/fog bank sits in valleys and above
 // water. Integrate height, including curvature, rather than a screen overlay.
 float fogColumn=0.,rainColumn=0.;
 if(uWeatherLayer.z>0.||uWeatherRain>.02)for(int i=0;i<4;++i){
  vec3 p=d*((float(i)+.5)*end*.25);
  float h=weatherHeight(p,uObserverHeight);
  fogColumn+=exp(-max(0.,h-uWeatherLayer.w)/.30);
  if(uWeatherRain>.02&&h<uWeatherLayer.x){
   // Distant precipitation curtains descend from the same cloud cells. From
   // airplane height these are volumes, not large raindrops on the camera.
   vec3 column=p+vec3(0.,uWeatherLayer.x+uWeatherLayer.y*.38-h,0.);
   float rain=weatherDensity(column,uObserverHeight,2.);
   rainColumn+=rain*smoothstep(0.,.20,h);
  }
 }
 float fogOptical=min(5.,end*.25*(fogColumn*uWeatherLayer.z+rainColumn*uWeatherRain*.025));
 float fogT=exp(-fogOptical);
 vec3 fogLight=viewSky+transmission(uTrans,uWeatherLayer.w,uSun.y)*
                max(0.,uSun.y)*uEclipse*.03+vec3(.97,.98,1.)*uPlanetLight*.15;
 // Foreground haze attenuates both the sky and clouds; no duplicate dry-air
 // scattering, which the terrain and atmosphere passes already integrate.
 L=L*fogT+fogLight*(1.-fogT);T*=fogT;
 return vec4(min(L*uSceneScale,vec3(60000.)),T);
}

void main(){
 vec3 d=weatherRay(uv);
 float coverage=0.;
 if(uv.y<uSurfaceFraction)coverage=texelFetch(uSurface,ivec2(gl_FragCoord.xy),0).a;
 bool edge=coverage>0.&&coverage<1.;
 float distance=weatherDepth(uv,d,coverage);
 color=integrateWeather(d,min(100.,distance));
 // Only partially covered pixels need both paths. The post pass combines their
 // original MSAA coverage, so opaque land cannot erase the cloudy sky beside it.
 skyWeather=vec4(0.,0.,0.,1.);
 if(edge)skyWeather=integrateWeather(d,100.);
}
