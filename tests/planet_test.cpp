#include "core/simulation/Simulator.h"
#include "core/physics/Gravity.h"
#include "core/simulation/RestrictedPlanet.h"
#include "renderer/common/SceneMesh.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <cstdio>
using namespace threebody;
void require(bool x,const char* msg){if(!x)throw std::runtime_error(msg);}
// Independent fixed-step coupled RK4 reference, evaluating the stars and tracer
// at the same stage times. This does not use the production Hermite interpolator.
ThreeBodyState<double> referenceStep(ThreeBodyState<double> s,double h){
  struct D { StateDerivative<double> stars;Vec3<double> p,v; };
  auto rhs=[](const ThreeBodyState<double>& y){D d;d.stars=evaluateDerivative(y);d.p=y.planet.velocity;d.v={};for(auto b:y.body){auto r=b.position-y.planet.position;auto n=norm(r);d.v+=r*(b.mass/(n*n*n));}return d;};
  auto add=[](ThreeBodyState<double> y,const D& d,double h){for(int i=0;i<3;++i){y.body[i].position+=d.stars.dPosition[i]*h;y.body[i].velocity+=d.stars.dVelocity[i]*h;}y.planet.position+=d.p*h;y.planet.velocity+=d.v*h;return y;};
  auto k1=rhs(s),k2=rhs(add(s,k1,h/2)),k3=rhs(add(s,k2,h/2)),k4=rhs(add(s,k3,h));
  s=add(s,k1,h/6);s=add(s,k2,h/3);s=add(s,k3,h/3);s=add(s,k4,h/6);s.simulationTime+=h;return s;
}
int main(){
  auto initial=makeReturningTriple();initializePlanet(initial);
  auto a=initial,b=initial,reference=initial;
  b.planetEnabled=false;
  double maxError=0,minDistance=1e9;
  for(int n=0;n<20000;++n){
    advanceSimulationRK45(a,.01);advanceSimulationRK45(b,.01);
    if(n<100){for(int k=0;k<1000;++k)reference=referenceStep(reference,.00001);maxError=std::max(maxError,norm(a.planet.position-reference.planet.position));}
    for(int i=0;i<3;++i){require(norm(a.body[i].position-b.body[i].position)==0 && norm(a.body[i].velocity-b.body[i].velocity)==0,"planet changed stellar trajectories");minDistance=std::min(minDistance,norm(a.planet.position-a.body[i].position));}
  }
  std::cout<<"Returning seed: t="<<a.simulationTime<<", Earth days="<<a.simulationTime*stellar::timeDays()<<", min separation AU="<<minDistance<<", planet error vs coupled RK4 h=1e-5 at t<=1 AU="<<maxError<<"\n";
  require(maxError<1e-7,"planet reference mismatch");
  auto refined=initial,coarse=initial;
  for(int n=0;n<100;++n)advanceSimulationRK45(coarse,.01);
  for(int n=0;n<2000;++n)advanceSimulationRK45(refined,.0005);
  const double refinement=norm(refined.planet.position-coarse.planet.position);
  std::cout << "stellar source step refinement .001 -> .0005, planet delta at t=1 AU=" << refinement << "\n";
  require(refinement<1e-7,"source interpolation refinement mismatch");
  // Contact must roll back the entire checkpoint (including the stars).
  auto close=initial;close.planet.position=close.body[0].position;auto before=close;bool rejected=false;
  try{advanceSimulationRK45(close,.01);}catch(const std::exception&){rejected=true;}
  require(rejected && close.simulationTime==before.simulationTime && norm(close.body[0].position-before.body[0].position)==0,"contact rollback");
  RenderFrameInput input;input.state=initial;
  auto history=std::make_shared<std::vector<ThreeBodyState<double>>>();history->push_back(initial);history->push_back(a);input.history=history;
  SceneMesh mesh;auto vertices=mesh.build(input);bool planet=false;
  for(const auto& v:vertices)if(v.color[3]<0)planet=true;
  require(planet && vertices.size()<=SceneMesh::maxVertices,"missing planet mesh");
  TrajectoryJournal journal;
  std::string directory="/tmp/threebody-planet-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  require(std::system(("mkdir -p "+directory).c_str())==0,"test directory");
  journal.configure(directory);journal.begin("{\"planetEnabled\":true}",initial);journal.append(a);journal.flush();
  std::ifstream csv(journal.exportFile(true));std::string header,row;std::getline(csv,header);std::getline(csv,row);
  require(header.find("planet_vz_km_s")!=std::string::npos,"CSV metadata");require(std::count(row.begin(),row.end(),',')==29,"CSV lost planet fields");
  std::ifstream json(journal.exportFile(false));std::string text((std::istreambuf_iterator<char>(json)),{});require(text.find("\"planetEnabled\":true")!=std::string::npos,"JSON metadata");
  std::cout<<"stellar bitwise equivalence, reference agreement, collision rollback, mesh, CSV/JSON PASS\n";
}
