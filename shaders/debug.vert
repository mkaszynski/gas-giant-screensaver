#version 300 es
layout(location=0) in vec2 p;
layout(location=1) in vec4 c;
uniform vec2 size;
out vec4 tint;
void main(){tint=c;gl_Position=vec4(p.x/size.x*2.-1.,1.-p.y/size.y*2.,0.,1.);}
