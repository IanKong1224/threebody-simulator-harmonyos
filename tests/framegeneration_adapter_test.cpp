#include "platform/harmony/GraphicsFrameGenerator.h"
#include "renderer/vulkan/VulkanRenderBackend.h"
#include "core/physics/InitialConditions.h"
#include <cstdlib>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
#include "framegeneration_probe.h"
extern "C" bool canIUse(const char*) { return true; }
using namespace threebody;
int main(){
 for(bool scaled:{false,true}) for(int mode=0;mode<3;++mode){
  if(mode==1)setenv("FG_TEST_UNSUPPORTED","1",1);else unsetenv("FG_TEST_UNSUPPORTED");
  if(mode==2)setenv("FG_TEST_DISPATCH_FAIL","1",1);else unsetenv("FG_TEST_DISPATCH_FAIL");
  int calls=0;bool destroyed=false;
  std::unique_ptr<VulkanUpscaler> upscale;
  if(scaled)upscale.reset(new Probe(false,calls,destroyed));
  VulkanRenderBackend backend(nullptr,std::move(upscale),makeGraphicsFrameGenerator());
  RenderConfig c;c.width=128;c.height=96;c.framesInFlight=2;
  assert(backend.initialize(c));assert(backend.frameGenerationActive()==(mode!=1));
  RenderFrameInput input;input.state=makeFigureEightDouble();input.allowFrameGeneration=true;
  auto render=[&]{auto f=backend.submitFrame(input);auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!backend.isFrameComplete(f)){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
  for(int n=0;n<6;++n)render();
  assert(backend.frameGenerationActive()==(mode==0));
  if(mode==0){
    auto pixels=backend.readbackForTest(true);assert(pixels.size()==128*96*4);
    for(size_t i=0;i<pixels.size();i+=4){assert(pixels[i]==32&&pixels[i+1]==128&&pixels[i+2]==223);}
    input.allowFrameGeneration=false;render();input.allowFrameGeneration=true;render();
    bool empty=false;try{backend.readbackForTest(true);}catch(...){empty=true;}assert(empty);
    for(int n=0;n<4;++n)render();assert(!backend.readbackForTest(true).empty());
  }
  backend.shutdown();
 }
 std::cout<<"SDK adapter contract: history collection, generated output, inverse matrix, pause reset, unsupported and dispatch fallback passed (mock SDK).\n";
}
