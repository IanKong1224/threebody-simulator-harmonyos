#include "core/physics/InitialConditions.h"
#include "core/simulation/Simulator.h"
#include <iostream>
using namespace threebody;
int main(){
 for(unsigned seed=0;seed<1000;++seed){
  auto s=makeRandomTriple(seed),copy=makeRandomTriple(seed),fixed=makeFigureEightDouble();Vec3<double>c{},p{};double k=0,u=0;
  if(!validateInitialState(s).empty())return 1;
  for(int i=0;i<3;++i){auto b=s.body[i];if(b.mass!=1||norm(b.velocity-fixed.body[i].velocity)!=0||norm(b.position-copy.body[i].position)!=0||norm(b.velocity-copy.body[i].velocity)!=0)return 2;
   c+=b.position*b.mass;p+=b.velocity*b.mass;k+=.5*b.mass*normSquared(b.velocity);
   for(int j=i+1;j<3;++j){double r=norm(b.position-s.body[j].position);if(r<.5)return 3;u+=b.mass*s.body[j].mass/r;}
  }
  if(norm(c)>1e-12||norm(p)>1e-12)return 4;
  advanceSimulation(s,.01);
 }
 if(norm(makeRandomTriple(1).body[0].position-makeRandomTriple(2).body[0].position)==0)return 5;
 std::cout<<"1000 seeds: mass, separation, finite state, barycenter, momentum, fixed velocities, reproducibility and first checkpoint PASS\n";
}
