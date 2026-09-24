uniform vec2 uResolution;
uniform vec3 uForward,uRight,uUp,uSun;
uniform sampler2D uSky,uTrans;
uniform float uTanFov,uExposure,uTime,uPlanetLight,uEclipse,uDarkGain;
vec3 ray(){vec2 p=(gl_FragCoord.xy/uResolution*2.-1.)*vec2(uResolution.x/uResolution.y,1.)*uTanFov;return normalize(uForward+uRight*p.x+uUp*p.y);}
// Decorative night lift is kept in display space; it must not drown physical
// emission or grow brighter when the dark-scene exposure increases.
vec3 sky(vec3 d){
 float night=1.-smoothstep(-.18,-.06,uSun.y);
 vec3 floorLight=vec3(.000020,.000032,.000050)*night*(1.+.6*pow(1.-abs(d.y),2.));
 floorLight+=vec3(.0000007,.000001,.000002)*(1.+uPlanetLight*25.);
 return texture(uSky,skyUV(d)).rgb+floorLight/uDarkGain;
}
vec3 viewT(vec3 d){return transmission(uTrans,.2,d.y);}
