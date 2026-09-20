// Kilometers, radiance in solar-irradiance-normalized units. Earth atmosphere.
const float PI=3.14159265359;
const float Rg=6371., Rt=6471.;
const vec3 betaR=vec3(.005802,.013558,.033100);
const vec3 betaM=vec3(.003996);
const vec3 betaO=vec3(.000650,.001881,.000085);
vec2 sphere(vec3 p,vec3 d,float r){float b=dot(p,d), c=dot(p,p)-r*r;float h=b*b-c; if(h<0.)return vec2(-1); return vec2(-b-sqrt(h),-b+sqrt(h));}
void medium(float h,out vec3 scattering,out vec3 extinction,out vec3 rayleigh,out vec3 mie){
 rayleigh=betaR*exp(-max(h,0.)/8.);
 mie=betaM*exp(-max(h,0.)/1.2);
 scattering=rayleigh+mie;
 extinction=rayleigh+mie*1.11+betaO*max(0.,1.-abs(h-25.)/15.);
}
vec3 transmission(sampler2D lut,float h,float mu){return texture(lut,vec2(mu*.5+.5,clamp(h/100.,0.,1.))).rgb;}
float phaseR(float c){return 3.*(1.+c*c)/(16.*PI);}
float phaseM(float c){float g=.8;return 3.*(1.-g*g)*(1.+c*c)/(8.*PI*(2.+g*g)*pow(max(.001,1.+g*g-2.*g*c),1.5));}
vec2 skyUV(vec3 d){float a=atan(d.z,d.x)/(2.*PI)+.5;float e=asin(clamp(d.y,-1.,1.));return vec2(a,.5+.5*sign(e)*sqrt(abs(e)/(PI*.5)));}
vec3 skyDirection(vec2 v){float a=(v.x-.5)*2.*PI;float e=(v.y-.5)*2.;e=sign(e)*e*e*PI*.5;return vec3(cos(a)*cos(e),sin(e),sin(a)*cos(e));}
