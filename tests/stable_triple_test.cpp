#include "core/physics/InitialConditions.h"
#include "core/physics/Invariants.h"
#include "core/physics/RK4Integrator.h"
#include "core/simulation/StellarUnits.h"
#include "core/simulation/Simulator.h"
#include <iostream>
#include <algorithm>
#include <cmath>
using namespace threebody;
int main(){
 auto a=makeStableTriple(),b=a;
 RK4Integrator<double> rk;
 const auto initial=computeInvariants(a);
 double minSep=1e9,maxRadius=0,maxDrift=0,maxDiff=0;
 // 630 internal units = approximately 100.27 Earth years.
 for(int n=0;n<6300000;++n){
  rk.step(a,1e-4);rk.step(b,5e-5);rk.step(b,5e-5);
  for(int i=0;i<3;++i){
   maxRadius=std::max(maxRadius,norm(a.body[i].position));
   maxDiff=std::max(maxDiff,norm(a.body[i].position-b.body[i].position));
   for(int j=i+1;j<3;++j){
    double r=norm(a.body[i].position-a.body[j].position);
    minSep=std::min(minSep,r);
    if(!std::isfinite(r)||r<=stellar::contactAU(a.body[i].mass,a.body[j].mass))return 1;
   }
  }
  if(n%100==0)maxDrift=std::max(maxDrift,std::abs((computeInvariants(a).totalEnergy-initial.totalEnergy)/initial.totalEnergy));
 }
 auto production=makeStableTriple();
 for(int n=1;n<=63000;++n) advanceSimulation(production,n*.01-production.simulationTime);
 double productionDiff=0;
 for(int i=0;i<3;++i) productionDiff=std::max(productionDiff,norm(production.body[i].position-b.body[i].position));
 std::cout<<"production checkpoint final difference AU="<<productionDiff<<"\n";
 if(productionDiff>1e-5)return 3;
 std::cout<<"stable spatial triple t=630 min separation AU="<<minSep<<" max radius AU="<<maxRadius<<" max relative energy drift="<<maxDrift<<" step-halving max position difference AU="<<maxDiff<<"\n";
 return maxDrift<1e-6&&maxDiff<1e-3&&maxRadius<3?0:2;
}
