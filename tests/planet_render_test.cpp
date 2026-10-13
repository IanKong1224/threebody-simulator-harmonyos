#include "renderer/vulkan/VulkanRenderBackend.h"
#include "renderer/gles/GLESRenderBackend.h"
#include "core/simulation/RestrictedPlanet.h"
#include "core/physics/InitialConditions.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace threebody;
template<class Backend>int run(const char* path){
 Backend backend(nullptr);RenderConfig config;config.width=640;config.height=480;config.framesInFlight=1;
 if(!backend.initialize(config))throw std::runtime_error("initialize");
 RenderFrameInput in;in.state=makeFigureEightDouble();initializePlanet(in.state);
 in.state.planet.position={1.7,0,0};in.state.body[0].position={3,1,0};in.state.body[1].position={-100,0,0};in.state.body[2].position={0,-100,0};
 in.showTrails=false;in.stellarBrightness=0;in.camera.distance=2.f;in.camera.yaw=0;in.camera.pitch=0;
 auto render=[&]{auto frame=backend.submitFrame(in);auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!backend.isFrameComplete(frame)){if(std::chrono::steady_clock::now()>until)throw std::runtime_error("timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}return backend.readbackForTest();};
 auto lit=render();std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";for(size_t i=0;i<lit.size();i+=4)f.write(reinterpret_cast<char*>(lit.data()+i),3);f.close();
 for(auto& b:in.state.body)b.mass=0;
 auto dark=render();const size_t middle=(240*640+320)*4;
 std::cerr << "lit=" << int(lit[middle]) << " dark=" << int(dark[middle]) << "," << int(dark[middle+1]) << "," << int(dark[middle+2]) << "\n";
 if(lit[middle]<20 || dark[middle]>1 || dark[middle+1]>1 || dark[middle+2]>1)throw std::runtime_error("planet is not reflecting external light only");
 std::cout<<"planet lit center="<<int(lit[middle])<<", no-source center="<<int(dark[middle])<<" PASS\n";
 return 0;
}
int main(int argc,char** argv){if(argc!=3)return 1;return std::string(argv[1])=="vk"?run<VulkanRenderBackend>(argv[2]):run<GLESRenderBackend>(argv[2]);}
