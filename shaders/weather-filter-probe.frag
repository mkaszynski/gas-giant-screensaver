#version 300 es
precision highp float;
uniform highp sampler3D uProbe;
out vec4 color;
void main(){
 float x=.45+gl_FragCoord.x/64.*.1;
 float filtered=textureLod(uProbe,vec3(x,.5,.5),0.).r;
 float expected=(64.+32.*(2.*x-.5))/255.;
 // A low-contrast ramp distinguishes quantized sample results from ordinary
 // interpolation-weight precision. Encode error in a portable RGBA8 readback.
 color=vec4(vec3(min(1.,abs(filtered-expected)*256.)),1.);
}
