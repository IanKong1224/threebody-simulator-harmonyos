#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=0) out vec3 vNormal;
layout(location=1) out vec4 vColor;
layout(push_constant) uniform Camera {mat4 mvp;vec4 params;} camera;
layout(location=2) out vec3 vViewDirection;
void main(){
 gl_Position=camera.mvp*vec4(position,1.0);
 gl_Position.y=-gl_Position.y;gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;
 vec4 eye=inverse(camera.mvp)*vec4(0.0,0.0,1.0,0.0);
 vViewDirection=eye.xyz/eye.w-position;
 vNormal=normal;vColor=color;
}
