#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec4 vColor;
layout(location=2) in vec3 vViewDirection;
layout(location=0) out vec4 outColor;
void main(){
 if(vColor.a>2.5){
   float r=length(vNormal.xy);
   if(r>=1.0)discard;
   float edge=1.0-smoothstep(0.55,1.0,r);
   outColor=vec4(clamp(vColor.rgb*edge,0.0,1.0),1.0);
 }else{
   vec3 n=normalize(vNormal);
   float light=0.30+0.70*max(dot(n,normalize(vec3(-0.4,-0.6,0.8))),0.0);
   outColor=vec4(vColor.rgb*light,1.0);
 }
}
