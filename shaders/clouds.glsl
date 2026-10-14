// One curved thin-cloud field is shared by sky, ground shadows and reflections.
uniform highp sampler2D uNoise;
uniform vec2 uCloudOffset[5];
uniform float uCloudLongitudeScale;
float n2(vec2 p,vec2 wind){return texture(uNoise,(mod(p,256.)+wind)/256.).r;}
float clouds(vec2 p){return .55*n2(p,uCloudOffset[0])+.27*n2(p*2.01,uCloudOffset[1])+.12*n2(p*4.03,uCloudOffset[2])+.06*n2(p*8.07,uCloudOffset[3]);}
float cloudCover(vec2 p){
 return smoothstep(.55,.79,clouds(p))*smoothstep(.33,.66,n2(p*.045+vec2(87.,31.),uCloudOffset[4]));
}
float cloudDensity(vec3 point,float elevation){
 vec2 p=point.xz*vec2(.85*uCloudLongitudeScale,1.9);
 // Thin sheets lengthen towards grazing angles. Fade the numerical far field
 // into the horizon rather than making a hard opaque cloud wall.
 float opticalDepth=.45*cloudCover(p)/max(.25,elevation);
 return (1.-exp(-opticalDepth))*smoothstep(.02,.15,elevation);
}
float cloudTransmission(vec3 point){
 if(uSun.y<=.02)return 1.;
 vec3 origin=vec3(0,Rg+uObserverHeight,0)+point;
 float distance=sphere(origin,uSun,Rg+8.).y;
 return 1.-cloudDensity(point+uSun*distance,uSun.y);
}
vec3 cloudSceneAt(vec3 d,vec3 L,vec3 origin){
 vec2 roots=sphere(vec3(0,Rg+uObserverHeight,0)+origin,d,Rg+8.);
 float dist=roots.y;vec3 q=origin+d*dist;
 vec2 p=q.xz*vec2(.85*uCloudLongitudeScale,1.9);
 float density=cloudDensity(q,d.y);
 // Coarse down-sun attenuation supplies depth without tracing a cloud volume.
 // One extra filtered lookup shades broad bodies; the fine field stays thin.
 float body=n2(p+uSun.xz*1.6,uCloudOffset[0]);
 float selfLight=mix(1.,.62,smoothstep(.52,.79,body));
 float sunlit=smoothstep(-.12,.06,uSun.y)*uEclipse;
 // Bounded forward scattering brightens edges towards the sun. Twilight hues
 // come from the actual atmospheric solar transmission, not a color preset.
 float forward=.65+.35*exp((clamp(dot(d,uSun),-1.,1.)-1.)*24.);
 vec3 cloudL=sky(d)*1.2+transmission(uTrans,8.,uSun.y)*sunlit*.16*selfLight*forward+vec3(.00002)*uPlanetLight;
 return mix(L,cloudL,density);
}
vec3 cloudScene(vec3 d,vec3 L){return cloudSceneAt(d,L,vec3(0));}
