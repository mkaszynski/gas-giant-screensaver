// Preserve the midpoint/Gaussian blend and its density-weighted centroid.
// A shared lookup loop avoids duplicating the large density function for
// every tap in the generated WebGPU shader. No sample is removed or moved.
float weatherStepDensity(vec3 d,float position,float stepLength,float lod,
                         inout float distance){
 if(stepLength<=.06)return marchDensity(d,distance,lod);
 float a=position+stepLength*.2113248654,b=position+stepLength*.7886751346;
 float weight=smoothstep(.06,.12,stepLength),da=0.,db=0.,middle=0.;
 int taps=weight<1.?3:2;
 for(int tap=0;tap<taps;++tap){
  float value=marchDensity(d,tap==0?a:tap==1?b:distance,lod);
  if(tap==0)da=value;else if(tap==1)db=value;else middle=value;
 }
 float density=.5*(da+db),moment=.5*(a*da+b*db);
 if(weight<1.){
  density=mix(middle,density,weight);
  moment=mix(distance*middle,moment,weight);
 }
 if(density>.00001)distance=moment/density;
 return density;
}
