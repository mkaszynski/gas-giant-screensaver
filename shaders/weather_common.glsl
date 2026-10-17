// Cached periodic 3D noise, fixed in planetary geographic coordinates. The CPU
// wraps offsets in double precision; local displacements keep Unix time stable.
#ifdef OBSERVATORY_WEATHER_MODE
// Cloud model selection is fixed at construction; discard the unused model
// and its register footprint while preserving dynamic weather parameters.
const bool uWeatherEnabled=OBSERVATORY_WEATHER_MODE!=0;
#else
uniform bool uWeatherEnabled;
#endif
uniform highp sampler3D uWeatherNoise;
uniform mat3 uWeatherFrame;
uniform vec3 uWeatherOffset,uWeatherRegionalOffset,uWeatherCirrusOffset;
// cover, storm, stratus, cirrus; base, thickness, fog extinction/km, fog top
uniform vec4 uWeatherKind,uWeatherLayer;
uniform float uWeatherRain;
uniform vec4 uWeatherFlash;
uniform vec3 uWeatherPlanetDirection;
vec3 weatherPoint(vec3 p){return uWeatherFrame*p;}
float weatherHeight(vec3 p,float observerHeight){
 return observerHeight+p.y+dot(p.xz,p.xz)/(2.*Rg);
}
// Stable shell intersections: subtract heights before multiplying radii, rather
// than subtracting two Earth-radius squared values in single precision.
vec2 weatherShell(vec3 origin,vec3 direction,float height,float observerHeight){
 float b=dot(vec3(0.,Rg+observerHeight,0.)+origin,direction);
 float c=(observerHeight-height)*(2.*Rg+observerHeight+height)
         +2.*(Rg+observerHeight)*origin.y+dot(origin,origin);
 float discriminant=b*b-c;
 if(discriminant<0.)return vec2(-1.);
 float root=sqrt(discriminant);
 // Stable small root at grazing angles and away from the near shell.
 float stable=-b-(b>=0.?root:-root);
 float other=abs(stable)>.00001?c/stable:0.;
 return vec2(min(stable,other),max(stable,other));
}
float weatherRegional(vec3 p,float observerHeight){
 return textureLod(uWeatherNoise,weatherPoint(p+vec3(0.,observerHeight,0.))/384.+uWeatherRegionalOffset,1.).r;
}
float weatherDensity(vec3 p,float observerHeight,float lod){
 float height=weatherHeight(p,observerHeight);
 float z=(height-uWeatherLayer.x)/uWeatherLayer.y;
 if(z<=0.||z>=1.)return 0.;
 vec3 q=weatherPoint(p+vec3(0.,observerHeight+height*2.,0.))/14.+uWeatherOffset;
 vec4 n=textureLod(uWeatherNoise,q,lod);
 // Eroded cellular detail sculpts sub-kilometre billows in the cloud skin.
 // Integer domain frequency retains seamless wind wrapping in the cache.
 float shape=dot(n,vec4(.55,.27,.12,.06));
 float regional=weatherRegional(p,observerHeight);
 float cover=clamp(uWeatherKind.x+(regional-.5)*.6,0.,1.);
 float threshold=mix(.79,.26,cover);
 // Erosion only removes density. Skip its lookup when neither the body nor
 // a storm anvil can contribute, preserving exactly the same density field.
 if(shape<=threshold&&(uWeatherKind.y<=0.||z<=.72||n.r<=threshold-.12))return 0.;
 vec4 detail=textureLod(uWeatherNoise,q*4.,min(5.,lod+2.));
 shape-=(1.-detail.g)*.075;
 // Flat condensation base and rounded, eroded tops. Only active convection
 // grows towers/anvils high enough to cover the gas giant for extended periods.
 float top=mix(.25+.75*smoothstep(.25,.72,n.r)+.32*(detail.g-.5),.94,uWeatherKind.z);
 top+=uWeatherKind.y*.35*smoothstep(.42,.72,n.r);
 float profile=smoothstep(0.,.12,z)*(1.-smoothstep(top-.30,top,z));
 float body=smoothstep(threshold,threshold+.085,shape)*profile;
 // A thin spreading anvil near the storm top, attached to the same cells.
 float anvil=smoothstep(.72,.87,z)*(1.-smoothstep(.92,1.,z))*
             smoothstep(threshold-.12,threshold+.04,n.r)*uWeatherKind.y;
 return max(body,anvil*.65)*(6.+8.*uWeatherKind.y+uWeatherKind.z);
}
float weatherCirrus(vec3 p){
 vec3 q=weatherPoint(p+vec3(0.,uObserverHeight,0.))/96.*vec3(1.,3.5,.7)+uWeatherCirrusOffset;
 // Wind-sheared filaments instead of a uniform veil.
 vec4 n=textureLod(uWeatherNoise,q,0.);
 vec4 detail=textureLod(uWeatherNoise,q+n.gba*.12,0.);
 float strands=smoothstep(.50,.67,.45*n.r+.35*n.g+.20*n.b);
 return strands*(.25+.75*detail.a)*uWeatherKind.w*.055;
}
float weatherShadowAt(vec3 p,float observerHeight){
 if(uSun.y<=.015||uEclipse<=0.)return 1.;
 float distance=weatherShell(p,uSun,uWeatherLayer.x+uWeatherLayer.y*.4,observerHeight).y;
 if(distance<=0.||distance>150.)return 1.;
 vec3 q=p+uSun*distance;
 float density=weatherDensity(q,observerHeight,1.);
 // Four representative strata approximate the filtered sun column cheaply.
 float optical=density*uWeatherLayer.y*.70/max(.16,uSun.y)*(1.-smoothstep(90.,150.,distance));
 return exp(-min(6.,optical));
}
struct WeatherLighting {
 vec3 ambient,planet;
 float forward,sunlit;
};
WeatherLighting weatherLighting(vec3 direction){
 WeatherLighting terms;
 terms.sunlit=smoothstep(-.08,.025,uSun.y)*uEclipse;
 float cosine=clamp(dot(direction,uSun),-1.,1.);
 terms.forward=.075+.17*pow(max(0.,cosine),12.);
 vec3 zenith=sky(vec3(0.,1.,0.));
 terms.ambient=mix(zenith,vec3(dot(zenith,vec3(.2126,.7152,.0722))),.45)*.95;
 float giantPhase=.12+.10*pow(max(0.,dot(direction,uWeatherPlanetDirection)),4.);
 terms.planet=vec3(.97,.98,1.)*uPlanetLight*giantPhase;
 return terms;
}
vec3 weatherLightPrepared(WeatherLighting terms,float height,vec2 attenuation){
 // Evaluate the height-dependent solar transmission once; the sky and phase
 // terms are invariant along a view ray and prepared outside the volume loop.
 vec3 solar=transmission(uTrans,height,uSun.y)*terms.sunlit;
 vec3 direct=solar*terms.forward*attenuation.x;
 vec3 ambient=terms.ambient+solar*.032*(.35+.65*attenuation.y);
 return direct+ambient+terms.planet;
}
vec3 weatherLightTrans(vec3 direction,float height,vec2 attenuation){
 return weatherLightPrepared(weatherLighting(direction),height,attenuation);
}
vec3 weatherLight(vec3 direction,float height,float optical){
 return weatherLightTrans(direction,height,exp(-optical*vec2(1.,.2)));
}
// Single-column approximation for water reflections; the main sky uses the
// resolved volume. Shared positions, cloud density, lighting and solar shadows.
vec3 weatherReflection(vec3 direction,vec3 L,vec3 origin){
 vec2 roots=weatherShell(origin,direction,uWeatherLayer.x+uWeatherLayer.y*.45,uObserverHeight);
 float distance=roots.x>0.?roots.x:roots.y;
 if(distance<=0.||distance>160.)return L;
 vec3 q=origin+direction*distance;
 float density=weatherDensity(q,uObserverHeight,1.);
 float optical=density*uWeatherLayer.y/max(.15,abs(direction.y))*(1.-smoothstep(100.,160.,distance));
 return mix(L,weatherLight(direction,uWeatherLayer.x,.6*density),1.-exp(-optical));
}
