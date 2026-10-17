#version 300 es
precision highp float;
layout(location=0) in vec2 aPosition;
layout(location=1) in vec4 aEndpoints;
layout(location=2) in vec4 aLight;
uniform vec2 uResolution;
flat out vec4 endpoints, light;
void main(){
 endpoints=aEndpoints;light=aLight;
 gl_Position=vec4(aPosition/uResolution*2.-1.,0.,1.);
}
