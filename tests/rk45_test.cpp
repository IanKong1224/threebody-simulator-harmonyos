#include "core/simulation/Simulator.h"
#include "core/physics/RK45Integrator.h"
#include "core/physics/RK4Integrator.h"
#include "core/physics/Invariants.h"
#include <iostream>
#include <stdexcept>
using namespace threebody;
int main(){
 RK4Integrator<double> ref;
 for(auto initial:{makeFigureEightDouble(),makeAsymmetricDemo(),makeStableTriple(),makeLongLivedTriple(),makeReturningTriple(),makeRandomTriple(7)}){
  auto s=initial,r=initial;double err=0;int count=0;
  for(int n=0;n<1200;++n){try{advanceSimulationRK45(s,.01);}catch(const std::runtime_error&){break;}
   for(int j=0;j<400;++j)ref.step(r,.000025);
   for(int i=0;i<3;i++)err=std::max(err,norm(s.body[i].position-r.body[i].position));count++;
  }
  std::cout<<"checkpoints="<<count<<" reference position error AU="<<err<<"\n";
  if(count<1||!std::isfinite(err)||err>1e-4)return 1;
 }
 auto s=makeReturningTriple();double e=computeInvariants(s).totalEnergy;for(int i=0;i<8000;i++)advanceSimulationRK45(s,.01);
 double drift=std::abs((computeInvariants(s).totalEnergy-e)/e);std::cout<<"returning t=80 energy drift="<<drift<<"\n";if(drift>1e-7)return 2;
 auto close=makeFigureEightDouble();close.body[0].position={-.005,0,0};close.body[1].position={.005,0,0};close.body[2].position={2,0,0};close.body[0].velocity={1,0,0};close.body[1].velocity={-1,0,0};auto before=close;
 bool rejected=false;try{advanceSimulationRK45(close,.01);}catch(const std::runtime_error&){rejected=true;}
 if(!rejected||close.simulationTime!=before.simulationTime||norm(close.body[0].position-before.body[0].position)!=0)return 3;
 auto trial1=rk45Trial(makeFigureEightDouble(),.05),trial2=rk45Trial(makeFigureEightDouble(),.025);
 if(!(trial1.error>1&&trial2.error<trial1.error/8))return 4;
}
