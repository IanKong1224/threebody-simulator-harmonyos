#version 450
layout(location=0) out vec2 uv;
void main(){
 int id=gl_VertexIndex;
 uv=vec2(float((id<<1)&2),float(id&2));gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
}
