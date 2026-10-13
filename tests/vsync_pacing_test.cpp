#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/simulation/Simulator.h"
#include "renderer/common/DisplayTickGate.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>
using namespace threebody;
struct Pacer {std::atomic<unsigned> tick{0};std::atomic<bool> cancelled{false};};
struct Output : IRenderBackend {
  Pacer& p;bool generation;uint64_t serial=0;unsigned last=0;std::atomic<unsigned> displayed{0};
  std::atomic<bool> shuttingDown{false};int shutdownDelayMs=0;
  Output(Pacer& pacer,bool gen):p(pacer),generation(gen){}
  bool initialize(const RenderConfig&)override{return true;}
  void shutdown()override{shuttingDown=true;std::this_thread::sleep_for(std::chrono::milliseconds(shutdownDelayMs));}
  BackendCapabilities queryCapabilities()const override{return {GraphicsBackendType::Vulkan,true,false,true,true};}
  FrameHandle submitFrame(const RenderFrameInput&)override{return {++serial};}
  bool isFrameComplete(FrameHandle)override{return true;}
  GpuTiming getGpuTiming(FrameHandle)override{return {};}
  void resizePreview(uint32_t,uint32_t)override{}
  void presentFrame(FrameHandle)override{if(!p.cancelled){assert(p.tick>last);last=p.tick;}++displayed;}
  bool presentIntermediateFrame(FrameHandle h)override{if(!generation)return false;presentFrame(h);return true;}
  bool frameGenerationActive()const override{return generation;}
};
int main(){
 DisplayTickGate gate;
 assert(!gate.consume(0));
 gate.signal(100,10);assert(gate.consume(105)); // preparation crossed a tick: no extra wait
 assert(!gate.consume(106)); // cannot double-submit in the same slot
 gate.signal(110,10);gate.signal(120,10);gate.signal(130,10);
 assert(gate.consume(131));assert(!gate.consume(132)); // skip missed ticks, no catch-up queue
 gate.signal(140,10);assert(!gate.consume(150)); // expired slot is never reused
 gate.signal(160,20);assert(gate.consume(175)); // accommodates system refresh-rate change
 gate.signal(180,20);gate.reset();assert(!gate.consume(181)); // pause discards pending slot

 auto& sim=Simulator::instance();
 for(bool generation:{false,true}){
  Pacer p;auto out=std::make_shared<Output>(p,generation);RenderConfig c;c.framesInFlight=2;c.targetFps=120;
  c.waitForDisplayTick=[&](const std::function<bool()>& cancel){
   std::this_thread::sleep_for(std::chrono::milliseconds(8));
   if(cancel()){p.cancelled=true;return false;}++p.tick;return true;
  };
  assert(sim.start(makeFigureEightDouble(),out,c));
  std::this_thread::sleep_for(std::chrono::milliseconds(160));sim.pause();sim.stop();
  assert(out->displayed>5);assert(out->displayed<=p.tick+1); // only a cancelled pair may drain
 }
 Pacer p;auto out=std::make_shared<Output>(p,false);RenderConfig c;
 out->shutdownDelayMs=250; // renderer teardown must not let physics erase its error
 c.waitForDisplayTick=[](const std::function<bool()>&){return false;};
 assert(sim.start(makeFigureEightDouble(),out,c));
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(!out->shuttingDown){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 std::this_thread::sleep_for(std::chrono::milliseconds(30));
 assert(sim.status().message.find("VSync")!=std::string::npos);assert(out->displayed==0);sim.stop();
 assert(sim.status().message.find("VSync")!=std::string::npos);
}
