// Shared cloud sheet: ground shadows sample the same field as visible clouds.
uniform highp sampler2D uNoise;
uniform vec2 uCloudOffset[5];
uniform float uCloudLongitudeScale;
float n2(vec2 p,vec2 wind){return texture(uNoise,(mod(p,256.)+wind)/256.).r;}
float clouds(vec2 p){return .55*n2(p,uCloudOffset[0])+.27*n2(p*2.01,uCloudOffset[1])+.12*n2(p*4.03,uCloudOffset[2])+.06*n2(p*8.07,uCloudOffset[3]);}
float cloudDensity(vec3 point,float elevation){
 vec2 p=point.xz*vec2(.85*uCloudLongitudeScale,1.9);
 return smoothstep(.55,.79,clouds(p))*smoothstep(.02,.15,elevation)*.26*
        smoothstep(.33,.66,n2(p*.045+vec2(87.,31.),uCloudOffset[4]));
}
float cloudTransmission(vec3 point){
 if(uSun.y<=.02)return 1.;
 vec3 origin=vec3(0,Rg+uObserverHeight,0)+point;
 float distance=sphere(origin,uSun,Rg+8.).y;
 return 1.-cloudDensity(point+uSun*distance,uSun.y);
}
