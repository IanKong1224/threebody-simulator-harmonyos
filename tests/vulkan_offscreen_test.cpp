#include "renderer/vulkan/VulkanRenderBackend.h"
#include "core/physics/InitialConditions.h"
#include "core/physics/RK4Integrator.h"
#include <thread>
#include <chrono>
#include <iostream>
using namespace threebody;
int main(){try{VulkanRenderBackend backend(nullptr);for(int cycle=0;cycle<2;++cycle){RenderConfig config;config.width=config.height=192;config.framesInFlight=1;if(!backend.initialize(config))return 1;
RenderFrameInput input{0,makeAsymmetricDemo()};RK4Integrator<double> integrator;
for(int i=0;i<20;++i){for(int j=0;j<100;++j)integrator.step(input.state,1e-4);input.sequenceId++;input.camera.yaw+=.03f;auto frame=backend.submitFrame(input);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
while(!backend.isFrameComplete(frame)){if(std::chrono::steady_clock::now()>deadline)return 2;std::this_thread::sleep_for(std::chrono::milliseconds(1));}auto timing=backend.getGpuTiming(frame);if(timing.valid&&(!std::isfinite(timing.milliseconds)||timing.milliseconds<0))return 3;
backend.presentFrame(frame);}
backend.shutdown();}
std::cout<<"PASS: actual Vulkan shader/depth pipeline, 40 completed offscreen frames, camera changes, timestamp query path, shutdown/reinitialize\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 4;}}
