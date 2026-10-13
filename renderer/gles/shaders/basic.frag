#version 300 es
precision highp float;
in vec3 local;
in vec4 tint;
layout(location=0) out vec4 outColor;
void main(){float coverage=1.0;if(tint.a>1.5){float r=length(local.xy);if(r>=1.0)discard;coverage=1.0-smoothstep(1.0-max(fwidth(r),.025),1.0,r);}outColor=vec4(tint.rgb*coverage,1.0);}
