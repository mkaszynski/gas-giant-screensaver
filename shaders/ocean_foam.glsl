// Prescribed stage-A/B whitecap coverage, filtered to a stable aerial mean.
// CPU uniforms contain measured wind coverage and an offline crest quantile.
uniform vec2 uWhitecaps;
uniform vec3 uFoamPhase;
float oceanFoamCoverage(vec4 phase,vec4 footprint){
 if(uWhitecaps.x<=0.)return 0.;
 // Breaking-wave groups are longer than the short wind waves supplying
 // reflection normals. Reuse their spatial directions with 192/310/470 m scales.
 const vec3 scale=vec3(48./192.,19./310.,7./470.);
 vec3 foot=footprint.xyz*scale;
 const vec3 weights=vec3(.58,.27,.15);
 float extent=max(1.e-6,1.-uWhitecaps.y);
 float curvature=dot(weights,foot*foot)*.125;
 // This lower bound proves the final blend is exactly the area mean.
 if(curvature>=2.*extent)return uWhitecaps.x;
 vec3 angles=mod(phase.xyz*scale,2.*PI)+uFoamPhase;
 float score=dot(weights,cos(angles));
 // Include curvature near a crest, where a first derivative vanishes.
 float width=dot(weights,abs(sin(angles))*foot)+curvature;
 float detailed=smoothstep(uWhitecaps.y-max(.002,width*.5),
                           uWhitecaps.y+max(.002,width*.5),score);
 // Unresolved breaking crests contribute their area average, not bright
 // isolated pixels. This has no frame noise, history, or camera-locked pattern.
 float unresolved=smoothstep(.5,2.,width/extent);
 return mix(detailed,uWhitecaps.x,unresolved);
}
