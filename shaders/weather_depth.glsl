uniform highp sampler2D uSurfaceDistance;
uniform float uSurfaceFraction;
vec3 weatherRay(vec2 p){
 vec2 q=(p*2.-1.)*vec2(uResolution.x/uResolution.y,1.)*uTanFov;
 return normalize(uForward+uRight*q.x+uUp*q.y);
}
float weatherDepth(vec2 p,vec3 d,float coverage){
 float distance=220.;
 if(p.y<uSurfaceFraction&&coverage>0.){
  float weighted=texelFetch(uSurfaceDistance,ivec2(gl_FragCoord.xy),0).r;
  distance=min(distance,weighted/coverage);
 }
 // Clip the inscribed ocean mesh against the exact sea sphere as well. This
 // prevents water LOD curvature error from changing the fog/cloud column.
 float sea=weatherShell(vec3(0.),d,0.,uObserverHeight).x;
 if(sea>0.)distance=min(distance,sea);
 return distance;
}
