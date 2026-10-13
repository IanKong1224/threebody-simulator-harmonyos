#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec4 vColor;
layout(location=2) in vec3 vViewDirection;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Camera {mat4 mvp;vec4 params;vec4 lights[3];} camera;
layout(location=3) in vec3 vWorldPosition;
#define starLights camera.lights
#define materialTime camera.params.x
#define stellarBrightness camera.params.y
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
