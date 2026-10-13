#pragma once
#include "Gravity.h"
#include <algorithm>
#include <cmath>
namespace threebody {
// Dormand-Prince 5(4), seven force evaluations per trial (no FSAL cache).
struct RK45Trial { ThreeBodyState<double> state; double error; };
inline RK45Trial rk45Trial(const ThreeBodyState<double>& s,double h,
                          double rtol=1e-10,double atol=1e-12) {
  const double a[7][7]={{0},{1./5},{3./40,9./40},{44./45,-56./15,32./9},
    {19372./6561,-25360./2187,64448./6561,-212./729},
    {9017./3168,-355./33,46732./5247,49./176,-5103./18656},
    {35./384,0,500./1113,125./192,-2187./6784,11./84}};
  const double low[7]={5179./57600,0,7571./16695,393./640,-92097./339200,187./2100,1./40};
  StateDerivative<double> k[7];
  auto high=s;
  for(int stage=0;stage<7;++stage){
    auto y=s;
    for(int j=0;j<stage;++j)for(int i=0;i<3;++i){
      y.body[i].position+=k[j].dPosition[i]*(h*a[stage][j]);
      y.body[i].velocity+=k[j].dVelocity[i]*(h*a[stage][j]);
    }
    k[stage]=evaluateDerivative(y);
    if(stage==6)high=y;
  }
  double error=0;
  for(int i=0;i<3;++i){
    Vec3<double> dp{},dv{};
    for(int j=0;j<7;++j){const double weight=(j<6?a[6][j]:0)-low[j];dp+=k[j].dPosition[i]*(h*weight);dv+=k[j].dVelocity[i]*(h*weight);}
    auto component=[&](double d,double old,double now){const double e=std::abs(d)/(atol+rtol*std::max(std::abs(old),std::abs(now)));if(!std::isfinite(e))error=INFINITY;else error=std::max(error,e);};
    const auto p=s.body[i].position,q=high.body[i].position,v=s.body[i].velocity,w=high.body[i].velocity;
    component(dp.x,p.x,q.x);component(dp.y,p.y,q.y);component(dp.z,p.z,q.z);
    component(dv.x,v.x,w.x);component(dv.y,v.y,w.y);component(dv.z,v.z,w.z);
  }
  high.simulationTime=s.simulationTime+h;
  return {high,error};
}
}
