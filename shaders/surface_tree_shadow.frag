#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTrees;
void main(){if(texture(uTrees,vUV).a<.5)discard;}
