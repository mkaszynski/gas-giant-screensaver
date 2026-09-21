uniform int uOccluderCount;
uniform vec4 uOccluders[21];
float sphereSunVisibility(vec3 p){
 float vis=1.;const float sr=.00465047;
 for(int i=0;i<21;++i){if(i>=uOccluderCount)break;vec3 v=uOccluders[i].xyz-p;float z=dot(v,uSun);if(z<=0.)continue;float dist=length(v);float r=asin(min(.999,uOccluders[i].w/dist));float d=acos(clamp(dot(v/dist,uSun),-1.,1.));
 if(d>=sr+r)continue;if(r>=d+sr)return 0.;if(sr>=d+r){vis=min(vis,1.-r*r/(sr*sr));continue;}
 float a=acos(clamp((d*d+sr*sr-r*r)/(2.*d*sr),-1.,1.));float b=acos(clamp((d*d+r*r-sr*sr)/(2.*d*r),-1.,1.));float area=sr*sr*a+r*r*b-.5*sqrt(max(0.,(-d+sr+r)*(d+sr-r)*(d-sr+r)*(d+sr+r)));vis=min(vis,1.-area/(PI*sr*sr));}
 return max(0.,vis);
}
