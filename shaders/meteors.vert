#version 300 es
precision highp float;
layout(location=0) in vec2 aPosition;
layout(location=1) in vec4 aEndpoints;
layout(location=2) in vec4 aLight;
layout(location=3) in float aTaper;
uniform vec2 uResolution;
flat out vec4 endpoints, light;
flat out float taper;
void main(){
 endpoints=aEndpoints;light=aLight;taper=aTaper;
 gl_Position=vec4(aPosition/uResolution*2.-1.,0.,1.);
}
