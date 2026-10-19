#version 300 es
layout(location=0) in vec2 p;
layout(location=1) in vec4 c;
uniform vec2 size;
uniform vec2 pixels;
out vec4 tint;
// Pixel boundaries avoid API-specific triangle edge ownership on bitmap glyphs
// when the scene is rendered below the logical window resolution.
void main(){tint=c;vec2 point=floor(p*pixels/size+.5)/pixels;gl_Position=vec4(point.x*2.-1.,1.-point.y*2.,0.,1.);}
