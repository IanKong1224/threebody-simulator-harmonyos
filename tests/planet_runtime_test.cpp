#include "core/simulation/Simulator.h"
#include "core/simulation/RestrictedPlanet.h"
#include "renderer/null/NullRenderBackend.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace threebody;
int main(){
 auto& sim=Simulator::instance();auto s=makeReturningTriple();initializePlanet(s);
 const std::string dir="/tmp/threebody-planet-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 if(std::system(("mkdir -p "+dir).c_str())!=0)return 1;
 sim.setRecordingDirectory(dir);auto backend=std::make_shared<NullRenderBackend>();RenderConfig c;
 if(!sim.start(s,backend,c,4,true,false))return 2;
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(sim.status().state.simulationTime<.2){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("runtime timeout");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 sim.pause();auto paused=sim.status().state;
 if(norm(paused.planet.position-s.planet.position)<.01)throw std::runtime_error("planet didn't move");
 auto path=sim.exportTrajectoryFile(false);std::cout<<"JSON="<<path<<"\n";
 sim.followPlanet(true);sim.setBrightness(.3f);sim.setCamera({0,0,2});
 std::this_thread::sleep_for(std::chrono::milliseconds(20));
 if(sim.status().state.simulationTime!=paused.simulationTime)throw std::runtime_error("paused camera advances physics");
 if(!sim.start(paused,backend,c,4,false,true))return 3;
 if(norm(sim.status().state.planet.position-paused.planet.position)!=0)throw std::runtime_error("renderer recreation resets planet");
 while(!sim.status().available){if(std::chrono::steady_clock::now()>deadline)return 4;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(!sim.resume())return 5;
 while(sim.status().state.simulationTime<=paused.simulationTime){if(std::chrono::steady_clock::now()>deadline)return 6;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 sim.pause();
 if(sim.status().state.simulationTime<=paused.simulationTime)throw std::runtime_error("resume failed");
 sim.stop();std::cout<<"planet worker, export, pause, continuation PASS\n";
}
