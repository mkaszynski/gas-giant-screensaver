uniform vec2 uResolution;
uniform vec3 uForward,uRight,uUp,uSun;
uniform sampler2D uSky,uTrans;
uniform float uTanFov,uExposure,uTime,uPlanetLight,uEclipse;
vec3 ray(){vec2 p=(gl_FragCoord.xy/uResolution*2.-1.)*vec2(uResolution.x/uResolution.y,1.)*uTanFov;return normalize(uForward+uRight*p.x+uUp*p.y);}
vec3 sky(vec3 d){return texture(uSky,skyUV(d)).rgb+vec3(.0000007,.000001,.000002)*(1.+uPlanetLight*25.);}
vec3 viewT(vec3 d){return transmission(uTrans,.2,d.y);}
