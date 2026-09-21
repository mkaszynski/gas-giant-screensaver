// Distribution of radial impact over a square pixel, after local linearization.
// This is the convolution of two uniform intervals, not a row of point samples.
float giantPixelCDF(float x,vec2 footprint){
 float a=max(max(footprint.x,footprint.y),1.e-8),b=min(footprint.x,footprint.y);
 float halfSpan=.5*(a+b);
 if(x<=-halfSpan)return 0.;if(x>=halfSpan)return 1.;
 if(abs(x)<=.5*(a-b))return .5+x/a;
 float tail=pow(halfSpan-abs(x),2.)/(2.*a*max(b,1.e-12));
 return x<0.?tail:1.-tail;
}
float giantPixelQuantile(float q,vec2 footprint){
 float a=max(max(footprint.x,footprint.y),1.e-8),b=min(footprint.x,footprint.y);
 float corner=b/(2.*a);
 if(q<corner)return -.5*(a+b)+sqrt(2.*a*b*q);
 if(q>1.-corner)return .5*(a+b)-sqrt(2.*a*b*(1.-q));
 return a*(q-.5);
}
float giantPixelPDF(float x,vec2 footprint){
 float a=max(max(footprint.x,footprint.y),1.e-8),b=min(footprint.x,footprint.y);
 if(abs(x)<=.5*(a-b))return 1./a;
 return max(0.,.5*(a+b)-abs(x))/(a*max(b,1.e-12));
}
vec3 giantFilteredRay(float b,float distance,vec3 axis,vec3 radial){
 float sine=min(.99999,b/distance);
 return axis*sqrt(1.-sine*sine)+radial*sine;
}
void giantFilteredAtmosphere(vec3 d,float impact,vec2 footprint,vec3 albedo,
                            float shadow,out vec3 light,out float opacity){
 vec3 axis=normalize(uBody.xyz),radial=normalize(d-axis*dot(d,axis));
 float distance=length(uBody.xyz)/uBody.w;
 vec3 center=uBody.xyz/uBody.w;
 float covered=giantPixelCDF(1.-impact,footprint);
 light=vec3(0.);opacity=covered;
 // Integrate the opaque cloud portion separately with exact area coverage.
 // Its two rays always hit the planet, even when the pixel center misses it.
 if(covered>0.){
   for(int k=0;k<2;++k){
     float q=covered*(k==0?.2113248654:.7886751346);
     float b=min(.9999999,impact+giantPixelQuantile(q,footprint));
     vec3 rd=giantFilteredRay(b,distance,axis,radial);
     float hit=dot(center,rd)-sqrt(max(0.,1.-b*b));
     vec3 n=normalize(rd*hit-center);
     vec3 cloudLight=albedo*(max(0.,dot(n,uSun))*shadow/PI+.0000003);
     vec3 sampleLight;float sampleAlpha;
     giantAtmosphere(rd,b,cloudLight,shadow,sampleLight,sampleAlpha);
     light+=sampleLight*(covered*.5);
   }
 }
 // Importance integration in density space guarantees samples in the thin
 // atmosphere. Uniform subpixel rays could all miss it, producing dotted arcs.
 float halfSpan=.5*(footprint.x+footprint.y);
 float lower=max(0.,impact-halfSpan-1.);
 float upper=min(giantTop,impact+halfSpan-1.);
 if(upper>lower){
   float t0=exp(-upper/giantH),t1=exp(-lower/giantH);
   const vec4 nodes=vec4(.0694318442,.3300094782,.6699905218,.9305681558);
   const vec4 weights=vec4(.1739274226,.3260725774,.3260725774,.1739274226);
   for(int k=0;k<4;++k){
     float t=mix(t0,t1,nodes[k]);
     float b=1.-giantH*log(t);
     vec3 rd=giantFilteredRay(b,distance,axis,radial);
     vec3 sampleLight;float sampleAlpha;
     giantAtmosphere(rd,b,vec3(0.),shadow,sampleLight,sampleAlpha);
     float weight=weights[k]*(t1-t0)*giantH/t*giantPixelPDF(b-impact,footprint);
     light+=sampleLight*weight;opacity+=sampleAlpha*weight;
   }
 }
 opacity=clamp(opacity,0.,1.);
}
