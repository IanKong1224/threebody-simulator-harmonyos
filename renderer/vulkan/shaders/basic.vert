#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=0) out vec3 local;
layout(location=1) out vec4 tint;
void main(){gl_Position=vec4(position,1.0);gl_Position.y=-gl_Position.y;gl_Position.z=(gl_Position.z+1.0)*.5;local=normal;tint=color;}
