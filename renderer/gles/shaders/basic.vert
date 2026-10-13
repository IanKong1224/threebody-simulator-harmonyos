#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
out vec3 local;
out vec4 tint;
void main(){gl_Position=vec4(position,1.0);local=normal;tint=color;}
