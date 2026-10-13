#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
out vec3 vNormal;
out vec4 vColor;
uniform mat4 mvp;
out vec3 vViewDirection;
void main(){
 gl_Position=mvp*vec4(position,1.0);
 vec4 eye=inverse(mvp)*vec4(0.0,0.0,1.0,0.0);
 vViewDirection=eye.xyz/eye.w-position;
 vNormal=normal;vColor=color;
}
