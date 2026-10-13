#version 300 es
precision highp float;
precision highp int;
out vec2 uv;
void main(){
 int id=gl_VertexID;
 uv=vec2(float((id<<1)&2),float(id&2));gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
}
