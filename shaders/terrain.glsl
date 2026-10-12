// Fixed-view geometry cache. Coordinates are angular, not screen rectangles,
// so aspect-ratio changes preserve the same landscape and projected normals.
uniform highp sampler2D uMountains,uTerrainNormal,uTerrainGeometry;
uniform highp sampler2D uHorizonA,uHorizonB;
uniform vec2 uWaterOffset;
vec3 terrainFrame(vec3 v){
 vec3 forward=normalize(vec3(uForward.x,0.,uForward.z));
 return vec3(dot(v,uRight),v.y,dot(v,forward));
}
vec2 terrainUV(vec3 d){
 vec3 v=terrainFrame(d);
 return vec2((atan(v.x,v.z)+1.134464014)/2.268928028,
             (asin(clamp(v.y,-1.,1.))+.209439510)/.820304748);
}
vec4 terrainSample(vec3 d){
 vec2 p=terrainUV(d);
 // Sample before branching: implicit mip derivatives are undefined when a
 // neighboring fragment returns early at the cache boundary.
 vec4 material=texture(uMountains,clamp(p,0.,1.));
 if(any(lessThan(p,vec2(0.)))||any(greaterThan(p,vec2(1.))))material=vec4(0.);
 return material;
}
float terrainHorizon(vec2 p,vec3 sun){
 vec4 a=texture(uHorizonA,p),b=texture(uHorizonB,p);
 float az=fract(atan(sun.x,sun.z)/(2.*PI))*8.;
 int i=int(floor(az));int j=(i+1)%8;
 float v=i<4?a[i]:b[i-4];float w=j<4?a[j]:b[j-4];
 return mix(v,w,fract(az))*(PI*.5);
}
vec3 terrainDiffuse(vec3 d,vec4 material){
 vec2 p=terrainUV(d);
 vec4 packedNormal=texture(uTerrainNormal,p);
 vec3 N=normalize(packedNormal.rgb*2.-1.);
 vec3 sun=terrainFrame(uSun);
 float horizon=terrainHorizon(p,sun);
 float shadow=smoothstep(horizon-.009,horizon+.009,asin(clamp(sun.y,-1.,1.)));
 vec3 direct=transmission(uTrans,.2,uSun.y)*max(dot(N,sun),0.)*shadow*uEclipse*.24;
 vec3 forward=normalize(vec3(uForward.x,0.,uForward.z));
 vec3 worldNormal=uRight*N.x+vec3(0,N.y,0)+forward*N.z;
 // Vertical rock faces receive the bright low sky, too. Sampling only the
 // zenith produces unnaturally navy cliffs despite a sunlit atmosphere.
 vec3 diffuseSky=sky(vec3(0,1,0))*.35+
                 sky(normalize(worldNormal+vec3(0,.35,0)))*.65;
 vec3 ambient=diffuseSky*2.2*packedNormal.a;
 // The giant supplies ambient fill using the existing phase/distance
 // estimate, keeping terrain dark during its eclipse.
 vec3 planet=vec3(.10,.16,.24)*uPlanetLight*.10*max(.2,N.y);
 vec3 light=direct+ambient+planet+vec3(.000040)/uDarkGain;
 vec3 rgb=material.rgb/max(material.a,1.e-5)*.72*light;
 vec4 geometry=texture(uTerrainGeometry,p);
 float distance=dot(geometry.rg,vec2(65280.,255.));
 vec3 T=exp(-distance*vec3(.000020,.000031,.000047));
 // Low-cost single-scattering aerial perspective: the directional atmosphere
 // LUT supplies the local haze color, rather than a permanent blue tint.
 return rgb*T+sky(d)*(1.-T);
}
vec3 reflectedScene(vec3 d){
 float z=dot(d,uForward);
 vec3 L=sky(d);
 if(z>0.){
  vec2 p=.5+.5*vec2(dot(d,uRight)/(z*uTanFov*uResolution.x/uResolution.y),dot(d,uUp)/(z*uTanFov));
  if(all(greaterThanEqual(p,vec2(0.)))&&all(lessThanEqual(p,vec2(1.))))L=sceneColor(p);
 }
 L=cloudScene(d,L);
 vec4 mountain=terrainSample(d);
 // Reflect dry terrain once. No recursion or second scene render.
 if(mountain.a>0.){
  float water=texture(uTerrainGeometry,terrainUV(d)).b;
  // Water/land mips encode area coverage. Thresholding it makes a moving
  // reflection jump between sky and rock as it crosses shoreline texels.
  L=mix(L,terrainDiffuse(d,mountain),mountain.a*(1.-water));
 }
 return L;
}
vec3 terrainSurface(vec3 d,vec4 mountain){
 vec2 p=terrainUV(d);
 vec4 geometry=texture(uTerrainGeometry,p);
 float water=geometry.b;
 vec3 land=terrainDiffuse(d,mountain);
 if(water<=0.)return land;
 float distance=dot(geometry.rg,vec2(65280.,255.));
 vec3 local=terrainFrame(d);
 vec2 world=local.xz*distance/max(length(local.xz),.001);
 // Two world-space wind waves, bounded clocks and derivatives. Their footprint
 // attenuation prevents glitter and aliasing at grazing angles or 4K resize.
 vec2 phase=world*vec2(.17,.23);
 vec2 footprint=fwidth(phase);
 vec2 amplitude=exp(-footprint*footprint*.5)*vec2(.003,.002);
 vec2 slope=vec2(sin(phase.x+phase.y*.4+uWaterOffset.x)*amplitude.x,
                 sin(phase.y-phase.x*.35+uWaterOffset.y)*amplitude.y);
 vec3 forward=normalize(vec3(uForward.x,0.,uForward.z));
 vec3 N=normalize(vec3(0,1,0)+uRight*slope.x+forward*slope.y);
 vec3 r=reflect(d,N);
 float fresnel=.0204+.9796*pow(1.-max(0.,dot(-d,N)),5.);
 vec3 reflection=reflectedScene(r);
 float sunAngle=acos(clamp(dot(r,uSun),-1.,1.));
 float solar=uEclipse*smoothstep(-.006,.008,uSun.y);
 vec4 sunTerrain=terrainSample(uSun);
 solar*=1.-sunTerrain.a;
 reflection+=transmission(uTrans,.2,uSun.y)*solar*.38*exp(-.5*pow(sunAngle/.009,2.));
 // Water is absorbing, not an emissive cyan overlay. The reflected sky and
 // sun follow actual visibility; cool subsurface color is lit by the sky.
 vec3 body=vec3(.006,.023,.028)*(sky(vec3(0,1,0))*.8+vec3(.00003)/uDarkGain);
 vec3 lake=reflection*fresnel+body*(1.-fresnel);
 return mix(land,lake,water);
}
