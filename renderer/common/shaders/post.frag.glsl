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
