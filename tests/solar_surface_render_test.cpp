#include "renderer/vulkan/VulkanRenderBackend.h"
#include "core/physics/InitialConditions.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <stdexcept>
using namespace threebody;
int main(){
 VulkanRenderBackend backend(nullptr);RenderConfig config;config.width=640;config.height=480;config.framesInFlight=1;
 backend.initialize(config);RenderFrameInput in;in.state=makeFigureEightDouble();
 in.state.body[0].position={1.65,0,0};in.state.body[1].position={20,0,0};in.state.body[2].position={-20,0,0};
 in.showTrails=false;in.stellarBrightness=0;in.camera.distance=2.f;in.camera.yaw=0;in.camera.pitch=0;
 auto frame=backend.submitFrame(in);auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(!backend.isFrameComplete(frame)){if(std::chrono::steady_clock::now()>until)throw std::runtime_error("timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 auto pixels=backend.readbackForTest();std::ofstream f("build-host/readbacks/solar-close.ppm",std::ios::binary);f<<"P6\n640 480\n255\n";
 for(size_t i=0;i<pixels.size();i+=4)f.write(reinterpret_cast<char*>(pixels.data()+i),3);
}
