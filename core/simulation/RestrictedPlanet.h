#pragma once
#include "core/physics/ThreeBodyState.h"
#include "core/simulation/StellarUnits.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace threebody {
// Earth-size test particle. Its mass is metadata only, with zero back-reaction.
inline void initializePlanet(ThreeBodyState<double>& s) {
  s.planetEnabled=true;
  s.planet.mass=3.00349e-6;
  s.planetRadius=6371000.0/stellar::auMeters;
  // A close circumstellar seed, not a promise of long-term stability/habitability.
  const double radius=.35, speed=std::sqrt(s.body[0].mass/radius);
  s.planet.position=s.body[0].position+Vec3<double>{0,0,radius};
  s.planet.velocity=s.body[0].velocity+Vec3<double>{speed,0,0};
}
inline Vec3<double> interpolatedStar(const ThreeBodyState<double>& a,
                                    const ThreeBodyState<double>& b,int i,double t) {
  const double h=b.simulationTime-a.simulationTime;
  const double u=std::max(0.,std::min(1.,(t-a.simulationTime)/h));
  const double u2=u*u,u3=u2*u;
  return a.body[i].position*(2*u3-3*u2+1)+a.body[i].velocity*(h*(u3-2*u2+u))+
         b.body[i].position*(-2*u3+3*u2)+b.body[i].velocity*(h*(u3-u2));
}
// Independent adaptive DP5(4) steps within each accepted stellar step.
// Cubic Hermite source interpolation has its own error; validated by step refinement.
inline void advancePlanet(const ThreeBodyState<double>& a,ThreeBodyState<double>& b,
                          double rtol=1e-10,double atol=1e-12,unsigned* checkpointTrials=nullptr) {
  if(!a.planetEnabled)return;
  auto p=a.planet;double t=a.simulationTime,h=b.simulationTime-t;
  const double weights[7][7]={{0},{1./5},{3./40,9./40},{44./45,-56./15,32./9},
    {19372./6561,-25360./2187,64448./6561,-212./729},
    {9017./3168,-355./33,46732./5247,49./176,-5103./18656},
    {35./384,0,500./1113,125./192,-2187./6784,11./84}};
  const double low[7]={5179./57600,0,7571./16695,393./640,-92097./339200,187./2100,1./40};
  const double times[7]={0,1./5,3./10,4./5,8./9,1,1};
  auto acceleration=[&](Vec3<double> pos,double time){
    Vec3<double> acc{};
    for(int i=0;i<3;++i){
      const auto d=interpolatedStar(a,b,i,time)-pos;const double distance=norm(d);
      if(!std::isfinite(distance))throw std::runtime_error("行星数值超出范围，已暂停");
      if(distance<=stellar::radiusAU(a.body[i].mass)+a.planetRadius)
        throw std::runtime_error("行星接近恒星表面，已暂停在接触前；不模拟碰撞或潮汐");
      acc+=d*(a.body[i].mass/(distance*distance*distance));
    }return acc;
  };
  unsigned trials=0;
  while(t<b.simulationTime){
    const double remaining=b.simulationTime-t;
    if(remaining<1e-14)break;
    h=std::min(h,remaining);
    for(int i=0;i<3;++i){
      const double d=norm(interpolatedStar(a,b,i,t)-p.position);
      const double v=norm(p.velocity-a.body[i].velocity);
      h=std::min(h,.01*std::sqrt(d*d*d/a.body[i].mass));
      if(v>0)h=std::min(h,.01*d/v);
    }
    if(++trials>10000 || (checkpointTrials && ++*checkpointTrials>20000) || (h<1e-10 && h<remaining))
      throw std::runtime_error("行星近距离运动超出计算预算，已暂停");
    Vec3<double> kp[7],kv[7];auto high=p;
    for(int stage=0;stage<7;++stage){
      auto y=p;
      for(int j=0;j<stage;++j){y.position+=kp[j]*(h*weights[stage][j]);y.velocity+=kv[j]*(h*weights[stage][j]);}
      kp[stage]=y.velocity;kv[stage]=acceleration(y.position,t+times[stage]*h);
      if(stage==6)high=y;
    }
    Vec3<double> dp{},dv{};
    for(int j=0;j<7;++j){const double w=(j<6?weights[6][j]:0)-low[j];dp+=kp[j]*(h*w);dv+=kv[j]*(h*w);}
    double error=0;
    auto component=[&](double d,double x,double y){double e=std::abs(d)/(atol+rtol*std::max(std::abs(x),std::abs(y)));error=std::isfinite(e)?std::max(error,e):INFINITY;};
    component(dp.x,p.position.x,high.position.x);component(dp.y,p.position.y,high.position.y);component(dp.z,p.position.z,high.position.z);
    component(dv.x,p.velocity.x,high.velocity.x);component(dv.y,p.velocity.y,high.velocity.y);component(dv.z,p.velocity.z,high.velocity.z);
    if(error<=1){p=high;t+=h;}
    h*=error>0?std::max(.2,std::min(5.,.9*std::pow(error,-.2))):5.;
  }
  b.planet=p;
}
}
