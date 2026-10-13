#pragma once
namespace threebody {
constexpr const char* scene_vert = R"GLSL(#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
out vec3 vNormal;
out vec4 vColor;
uniform mat4 mvp;
out vec3 vViewDirection;
out vec3 vWorldPosition;
void main(){
 vWorldPosition=position;
 gl_Position=mvp*vec4(position,1.0);
 vec4 eye=inverse(mvp)*vec4(0.0,0.0,1.0,0.0);
 vViewDirection=eye.xyz/eye.w-position;
 vNormal=normal;vColor=color;
}
)GLSL";
constexpr const char* scene_frag = R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec3 vNormal;
in vec4 vColor;
in vec3 vViewDirection;
layout(location=0) out vec4 outColor;
uniform float materialTime;
uniform float stellarBrightness;
in vec3 vWorldPosition;
uniform vec4 starLights[3];
// Shared reduced photosphere model, not a magnetohydrodynamic simulation.
// All evolution follows simulation time; frozen redraws remain deterministic.
vec3 hash3(vec3 p) {
 p=fract(p*vec3(.1031,.1030,.0973));
 p+=dot(p,p.yxz+33.33);
 return fract((p.xxy+p.yxx)*p.zyx);
}
float thermalNoise(vec3 p) {
 vec3 cell=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
 return mix(mix(mix(hash3(cell).x,hash3(cell+vec3(1,0,0)).x,f.x),
                mix(hash3(cell+vec3(0,1,0)).x,hash3(cell+vec3(1,1,0)).x,f.x),f.y),
            mix(mix(hash3(cell+vec3(0,0,1)).x,hash3(cell+vec3(1,0,1)).x,f.x),
                mix(hash3(cell+vec3(0,1,1)).x,hash3(cell+vec3(1,1,1)).x,f.x),f.y),f.z);
}
// Approximate visible-band blackbody tint; illustrative, not calibrated photometry.
vec3 thermalTint(float temperature) {
 vec3 wavelength=vec3(650.0,550.0,450.0);
 vec3 intensity=pow(vec3(550.0)/wavelength,vec3(5.0)) /
                (exp(vec3(1.4388e7)/(wavelength*temperature))-1.0);
 return intensity/max(intensity.r,max(intensity.g,intensity.b));
}
float granules(vec3 p, float time) {
 vec3 cell=floor(p), local=fract(p);
 float nearest=10.0, second=10.0;
 for(int z=-1;z<=1;++z) for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
  vec3 offset=vec3(float(x),float(y),float(z));
  vec3 seed=hash3(cell+offset);
  // Slowly evolving cell centers approximate the surface pattern of convection.
  vec3 center=.5+.32*sin(seed*6.2831853+time*.3);
  vec3 delta=offset+center-local;
  float d=dot(delta,delta);
  if(d<nearest) {second=nearest;nearest=d;} else second=min(second,d);
 }
 // Smooth bright cell interiors, darker intergranular lanes; avoid flat cracked plates.
 float lanes=smoothstep(.015,.18,sqrt(second)-sqrt(nearest));
 float dome=1.0-smoothstep(0.0,.8,nearest);
 return mix(.12,.95,lanes)*(.7+.3*dome);
}
void main() {
 vec3 radiance;
 if(vColor.a<-.5) {
  vec3 n=normalize(vNormal);
  // Procedural rock, diffuse reflection only; no self-emission or hardware RT.
  float a=materialTime*6.0,c=cos(a),s=sin(a);
  vec3 local=vec3(c*n.x-s*n.y,s*n.x+c*n.y,n.z);
  float terrain=.65*thermalNoise(local*8.0)+.25*thermalNoise(local*30.0)+.10*thermalNoise(local*90.0);
  vec3 albedo=mix(vec3(.12,.095,.07),vec3(.48,.40,.30),smoothstep(.2,.8,terrain));
  radiance=vec3(0.0);
  for(int i=0;i<3;++i){
    vec3 d=starLights[i].xyz-vWorldPosition;
    float d2=max(dot(d,d),.00001);
    vec3 tint=i==0?vec3(1.0,.88,.64):(i==1?vec3(.62,.80,1.0):vec3(1.0,.96,.82));
    radiance+=albedo*tint*max(dot(n,d*inversesqrt(d2)),0.0)*starLights[i].w*.35/d2;
  }
 } else if(vColor.a>2.5) {
  float radius=length(vNormal.xy);
  if(radius>=1.0) discard;
  float edge=max(fwidth(radius),.025);
  float coverage=1.0-smoothstep(1.0-edge,1.0,radius);
  radiance=vColor.rgb*3.5*coverage*(1.0-.45*radius*radius);
 } else if(vColor.a>1.5) {
  radiance=vColor.rgb*3.5;
 } else if(dot(vNormal,vNormal)>.01) {
  vec3 n=normalize(vNormal);
  bool hot=vColor.b>vColor.r;
  bool cool=vColor.g<.5;
  float baseTemperature=hot?9000.0:(cool?4700.0:5800.0);
  float identity=hot?2.7:(cool?.3:5.1);
  // Differential rotation is illustrative: normalized orbit time is not seconds.
  float angle=materialTime*.10*(1.0-.22*n.z*n.z)+identity;
  float c=cos(angle),s=sin(angle);
  vec3 surface=vec3(c*n.x-s*n.y,s*n.x+c*n.y,n.z);
  // Solar-inspired, artistically enlarged photospheric features; not MHD.
  // Evaluate footprints before conditional detail work to keep derivatives valid.
  vec3 p=surface*32.0+vec3(identity*13.0);
  vec3 fineP=surface*96.0+vec3(identity*19.0);
  float coarseFootprint=max(length(dFdx(p)),length(dFdy(p)));
  float fineFootprint=max(length(dFdx(fineP)),length(dFdy(fineP)));
  float diskPixels=2.0/max(max(length(dFdx(n)),length(dFdy(n))),.00001);
  float resolvedSurface=smoothstep(12.0,80.0,diskPixels);
  float coarseWeight=1.0-smoothstep(.45,1.4,coarseFootprint);
  float fineWeight=1.0-smoothstep(.35,1.2,fineFootprint);
  float cellPattern=.55, finePattern=.55;
  if(coarseWeight>.001) cellPattern=mix(.55,granules(p,materialTime),coarseWeight);
  if(fineWeight>.001) finePattern=mix(.55,granules(fineP,materialTime*.73),fineWeight);
  float activity=thermalNoise(surface*9.0+vec3(identity));
  float pores=0.0, micro=.5;
  if(fineWeight>.001) {
    pores=smoothstep(.76,.91,thermalNoise(surface*48.0+vec3(identity)))*fineWeight;
    micro=mix(.5,thermalNoise(surface*160.0+vec3(identity)),fineWeight);
  }
  float convection=((cellPattern-.55)*.12+(finePattern-.55)*.055+(micro-.5)*.015)*resolvedSurface;
  float spotCooling=0.0, faculae=0.0;
  // A handful of illustrative active regions. Tangent-plane coordinates avoid UV seams.
  for(int spot=0;spot<6;++spot) {
    vec3 seed=hash3(vec3(float(spot),identity,17.0));
    float longitude=seed.x*6.2831853;
    float latitude=(seed.y-.5)*.9;
    vec3 center=vec3(cos(longitude)*cos(latitude),sin(longitude)*cos(latitude),sin(latitude));
    vec3 east=vec3(-sin(longitude),cos(longitude),0.0);
    vec3 north=cross(center,east);
    vec2 q=vec2(dot(surface,east),dot(surface,north));
    float radius=.045+.035*seed.z;
    q/=radius;
    float r=length(q);
    float front=smoothstep(.8,.95,dot(surface,center));
    float phi=atan(q.y,q.x+0.00001);
    float filamentWeight=1.0-smoothstep(.04,.22,max(length(dFdx(q)),length(dFdy(q))));
    float filaments=.5+.5*sin(phi*34.0+5.0*r+seed.z*12.0)*filamentWeight;
    float penumbra=(1.0-smoothstep(.62,1.15,r))*front;
    float umbra=1.0-smoothstep(.30,.52,r);
    spotCooling=max(spotCooling,penumbra*(.10+.20*umbra+.035*filaments));
    float ring=smoothstep(.7,1.05,r)*(1.0-smoothstep(1.15,1.8,r))*front;
    faculae=max(faculae,ring*(.5+.5*activity));
  }
  float mu=clamp(dot(n,normalize(vViewDirection)),0.0,1.0);
  float brightening=faculae*(.015+.035*(1.0-mu))*resolvedSurface;
  float temperature=baseTemperature*(1.0+convection-spotCooling*resolvedSurface-.035*pores*resolvedSurface+brightening);
  // Linear limb darkening: self-emission fades toward the observed disk edge.
  float u=hot?.42:.65;
  float limb=1.0-u*(1.0-mu);
  float flux=pow(temperature/baseTemperature,4.0);
  radiance=thermalTint(temperature)*1.8*flux*limb*(.35+.65*stellarBrightness);
 } else {
  radiance=vColor.rgb*.85;
 }
 outColor=vec4(radiance,vColor.a<-.5?0.0:1.0);
}
)GLSL";
constexpr const char* post_vert = R"GLSL(#version 300 es
precision highp float;
precision highp int;
out vec2 uv;
void main(){
 int id=gl_VertexID;
 uv=vec2(float((id<<1)&2),float(id&2));gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
}
)GLSL";
constexpr const char* post_frag = R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec2 uv;
layout(location=0) out vec4 outColor;
uniform sampler2D sceneTexture;
uniform sampler2D bloomTexture;
uniform vec4 postParams;
uniform vec4 haloStars[3];
#define POST_FLIP_Y 0
// Shared 6-pass profile: scene, quarter-size extraction, X/Y/X blur,
// then final Y blur + halo composition + tone mapping.
vec3 bright(vec3 c) {
 float l=max(c.r,max(c.g,c.b));
 return c*max(l-0.7,0.0)/max(l,0.00001);
}
vec3 srgb(vec3 c) {
 return mix(c*12.92,1.055*pow(max(c,vec3(0.0)),vec3(1.0/2.4))-0.055,step(vec3(0.0031308),c));
}
// Static screen-space dithering at final 8-bit encoding only. No temporal
// flicker, no extra texture/pass, and exact-zero background remains black.
vec3 encodeSmooth(vec3 linearColor) {
 vec3 encoded=srgb(linearColor);
 ivec2 extent=textureSize(sceneTexture,0);
 ivec2 pixel=ivec2(floor(uv*vec2(extent)));
 if(POST_FLIP_Y==1) pixel.y=extent.y-1-pixel.y;
 uint bits=uint(pixel.x)*1973u+uint(pixel.y)*9277u+89173u;
 bits^=bits>>16; bits*=2246822519u; bits^=bits>>13;
 float noise=(float(bits&65535u)/65535.0-.5)/255.0;
 encoded=clamp(encoded+vec3(noise)*step(vec3(.000001),encoded),0.0,1.0);
 if(postParams.y>.5) return encoded;
 // sRGB attachment will encode again: feed it inverse-transformed values.
 return mix(encoded/12.92,pow((encoded+.055)/1.055,vec3(2.4)),
            step(vec3(.04045),encoded));
}
// Higher tiers use denser HDR targets and wider normalized kernels.
// Every texture tap contributes to the visible glow; zero input stays zero.
vec3 qualityBlur(sampler2D tex, vec2 direction, int radius) {
 vec2 d=direction*1.0/vec2(textureSize(tex,0));
 float sigma=float(radius)*0.28;
 vec3 c=vec3(0.0);float weight=0.0;
 for(int i=-24;i<=24;++i) {
  if(abs(i)>radius) continue;
  float w=exp(-0.5*float(i*i)/(sigma*sigma));
  c+=texture(tex,uv+d*float(i)).rgb*w;weight+=w;
 }
 return c/weight;
}
// Illustrative optical halo: smoothly vanishes at eight times the projected radius.
// Scaling by each star's radius avoids a fixed-pixel bright rim when zooming.
vec3 stellarGlow() {
 float aspect=float(textureSize(sceneTexture,0).x)/float(textureSize(sceneTexture,0).y);
 vec3 result=vec3(0.0);
 for(int i=0;i<3;++i) {
  vec4 star=haloStars[i];
  if(star.w<.5 || star.z<=0.0) continue;
  vec2 delta=(uv-star.xy)*vec2(aspect,1.0);
  float r=length(delta)/star.z;
  // One monotonic radial profile: no flat core, shoulders or layer seams.
  // At the finite support edge the value is negligible before the soft cutoff.
  float halo=exp(-.28*r*r);
  halo*=1.0-smoothstep(7.0,8.0,r);
  vec3 tint=i==0?vec3(1.0,.88,.64):(i==1?vec3(.62,.80,1.0):vec3(1.0,.96,.82));
  result+=tint*halo*9.0*postParams.w*postParams.w;
 }
 return result;
}
void main() {
 int mode=int(postParams.x+0.5);
 vec3 color;
 if(mode==0) {
  vec2 d=1.0/vec2(textureSize(sceneTexture,0));
  color=(bright(texture(sceneTexture,uv+vec2(-1.0,-1.0)*d).rgb)
       + bright(texture(sceneTexture,uv+vec2(1.0,-1.0)*d).rgb)
       + bright(texture(sceneTexture,uv+vec2(-1.0,1.0)*d).rgb)
       + bright(texture(sceneTexture,uv+vec2(1.0,1.0)*d).rgb))*0.25;
 } else if(mode==2 || mode==3) {
  color=qualityBlur(sceneTexture,mode==2?vec2(0.0,1.0):vec2(1.0,0.0),12);
 } else if(mode==1 && postParams.z>0.5) {
  color=qualityBlur(sceneTexture,vec2(1.0,0.0),postParams.z>1.5?24:12);
 } else if(mode==1) {
  vec2 d=vec2(1.0/float(textureSize(sceneTexture,0).x),0.0);
  color=texture(sceneTexture,uv).rgb*0.2270270270;
  color+=(texture(sceneTexture,uv+d).rgb+texture(sceneTexture,uv-d).rgb)*0.1945945946;
  color+=(texture(sceneTexture,uv+2.0*d).rgb+texture(sceneTexture,uv-2.0*d).rgb)*0.1216216216;
  color+=(texture(sceneTexture,uv+3.0*d).rgb+texture(sceneTexture,uv-3.0*d).rgb)*0.0540540541;
  color+=(texture(sceneTexture,uv+4.0*d).rgb+texture(sceneTexture,uv-4.0*d).rgb)*0.0162162162;
 } else {
  vec2 d=vec2(0.0,1.0/float(textureSize(bloomTexture,0).y));
  vec3 glow=texture(bloomTexture,uv).rgb*0.2270270270;
  glow+=(texture(bloomTexture,uv+d).rgb+texture(bloomTexture,uv-d).rgb)*0.1945945946;
  glow+=(texture(bloomTexture,uv+2.0*d).rgb+texture(bloomTexture,uv-2.0*d).rgb)*0.1216216216;
  glow+=(texture(bloomTexture,uv+3.0*d).rgb+texture(bloomTexture,uv-3.0*d).rgb)*0.0540540541;
  glow+=(texture(bloomTexture,uv+4.0*d).rgb+texture(bloomTexture,uv-4.0*d).rgb)*0.0162162162;
  if(postParams.z>0.5)glow=qualityBlur(bloomTexture,vec2(0.0,1.0),postParams.z>1.5?24:12);
  vec3 surface=texture(sceneTexture,uv).rgb;
  // Let glare overlap the photosphere instead of cutting it out at the limb.
  vec3 hdr=surface+.85*glow+stellarGlow()*texture(sceneTexture,uv).a;
  color=vec3(1.0)-exp(-hdr*0.8);
  color=encodeSmooth(color);
 }
 outColor=vec4(color,1.0);
}
)GLSL";
constexpr const char* basic_vert = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
out vec3 local;
out vec4 tint;
void main(){gl_Position=vec4(position,1.0);local=normal;tint=color;}
)GLSL";
constexpr const char* basic_frag = R"GLSL(#version 300 es
precision highp float;
in vec3 local;
in vec4 tint;
layout(location=0) out vec4 outColor;
void main(){float coverage=1.0;if(tint.a>1.5){float r=length(local.xy);if(r>=1.0)discard;coverage=1.0-smoothstep(1.0-max(fwidth(r),.025),1.0,r);}outColor=vec4(tint.rgb*coverage,1.0);}
)GLSL";
constexpr const char* particles_vert = R"GLSL(#version 300 es
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
)GLSL";
constexpr const char* particles_frag = R"GLSL(#version 300 es
precision highp float;
in vec3 vNormal;
in vec4 vColor;
in vec3 vViewDirection;
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
)GLSL";
}
