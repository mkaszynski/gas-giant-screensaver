uniform sampler2D uRingProfile;
uniform vec3 uRingCenter,uRingNormal;
uniform vec2 uRingBounds;
uniform bool uRingsEnabled;

float ringTau(float radius,float lod){
 if(radius<uRingBounds.x||radius>uRingBounds.y)return 0.;
 return textureLod(uRingProfile,vec2((radius-uRingBounds.x)/(uRingBounds.y-uRingBounds.x),.5),lod).r;
}
float ringSunTransmission(vec3 point,vec3 sun){
 if(!uRingsEnabled)return 1.;
 float mu=dot(uRingNormal,sun);
 if(abs(mu)<1.e-7)return 1.;
 float t=-dot(point-uRingCenter,uRingNormal)/mu;
 if(t<=0.)return 1.;
 float radius=length(point+sun*t-uRingCenter);
 float footprint=t*.00465047/abs(mu);
 // Pixel filtering suppresses crawling ringlet shadows. Three samples soften
 // band boundaries across the finite solar disk without any shadow-map pass.
 float lod=max(0.,log2(max(fwidth(radius),footprint*.35)*4096./(uRingBounds.y-uRingBounds.x)));
 float invMu=1./abs(mu);
 return .5*exp(-ringTau(radius,lod)*invMu)+
        .25*exp(-ringTau(radius-footprint*.7,lod)*invMu)+
        .25*exp(-ringTau(radius+footprint*.7,lod)*invMu);
}
