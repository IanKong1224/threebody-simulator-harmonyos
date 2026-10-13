#include "core/physics/InitialConditions.h"
#include "core/physics/RK4Integrator.h"
#include "core/physics/Invariants.h"
#include "core/simulation/Simulator.h"
#include <iostream>
#include <algorithm>
using namespace threebody;
int main(){auto a=makeReturningTriple(),b=a,c=a;RK4Integrator<double>rk;const double E=computeInvariants(a).totalEnergy;double minR=100,maxR=0,maxError=0,maxDrift=0,fineError=0;bool away[3]={};int returns[3]={},changes=0,prev=-1;
for(int n=1;n<=8000;n++){advanceSimulation(a,n*.01-a.simulationTime);for(int j=0;j<200;j++)rk.step(b,.00005);for(int j=0;j<400;j++)rk.step(c,.000025);double near=100;int pair=-1;
for(int i=0;i<3;i++){fineError=std::max(fineError,norm(b.body[i].position-c.body[i].position));const double r=norm(a.body[i].position);maxR=std::max(maxR,r);if(r>1.5)away[i]=true;if(r<.8&&away[i]){returns[i]++;away[i]=false;}maxError=std::max(maxError,norm(a.body[i].position-b.body[i].position));for(int j=i+1;j<3;j++){double sep=norm(a.body[i].position-a.body[j].position);minR=std::min(minR,sep);if(sep<near){near=sep;pair=i*3+j;}}}
if(pair!=prev)changes++;prev=pair;maxDrift=std::max(maxDrift,std::abs((computeInvariants(a).totalEnergy-E)/E));}
std::cout<<"t=80 min separation="<<minR<<" max radius="<<maxR<<" max drift="<<maxDrift<<" max reference difference="<<maxError<<" fine reference difference="<<fineError<<" nearest changes="<<changes<<" returns="<<returns[0]<<","<<returns[1]<<","<<returns[2]<<"\n";
return minR>.05&&maxR<4&&maxDrift<1e-6&&maxError<1e-3&&fineError<maxError/4&&changes>20&&returns[0]>0&&returns[1]>0&&returns[2]>0?0:1;}
