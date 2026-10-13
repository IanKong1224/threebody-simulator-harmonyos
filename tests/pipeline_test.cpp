#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/simulation/Simulator.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
using namespace threebody;
using Clock=std::chrono::steady_clock;
struct QueuedDisplay final : IRenderBackend {
  struct Slot { uint64_t serial=0; Clock::time_point ready{}; bool queued=false; };
  std::array<Slot,2> slots;
  uint64_t serial=0;
  std::atomic<unsigned> submitted{0}, timed{0}, overlap{0}, stopped{0};
  bool initialize(const RenderConfig& c) override {return c.framesInFlight==2;}
  BackendCapabilities queryCapabilities() const override {return {GraphicsBackendType::Vulkan,true,true,true,true};}
  FrameHandle submitFrame(const RenderFrameInput&) override {
    auto& slot=slots[serial%2];
    assert(!slot.serial || (slot.queued && Clock::now()>=slot.ready));
    auto& other=slots[(serial+1)%2];
    if(other.serial && Clock::now()<other.ready)++overlap;
    slot.serial=++serial;slot.queued=false;
    slot.ready=Clock::now()+std::chrono::milliseconds(60);
    ++submitted;return {serial};
  }
  bool isFrameComplete(FrameHandle h) override {return Clock::now()>=slots[(h.value-1)%2].ready;}
  void presentFrame(FrameHandle h) override {
    auto& slot=slots[(h.value-1)%2];assert(slot.serial==h.value&&!slot.queued);slot.queued=true;
  }
  bool isPresentationComplete(FrameHandle h) override {
    auto& slot=slots[(h.value-1)%2];assert(slot.serial==h.value);return slot.queued && Clock::now()>=slot.ready;
  }
  GpuTiming getGpuTiming(FrameHandle h) override {
    assert(isPresentationComplete(h));++timed;return {true,60};
  }
  void resizePreview(uint32_t,uint32_t) override {}
  void shutdown() override {++stopped;}
};
template<class F> void until(F f) {
  auto deadline=Clock::now()+std::chrono::seconds(3);
  while(!f()){assert(Clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
int main(){
  auto& sim=Simulator::instance();auto display=std::make_shared<QueuedDisplay>();
  RenderConfig config;config.framesInFlight=2;config.targetFps=120;config.enableGpuTiming=true;
  assert(sim.start(makeFigureEightDouble(),display,config));
  until([&]{return display->timed>=5;});
  assert(display->overlap>0);
  sim.pause();auto time=sim.status().state.simulationTime;
  std::this_thread::sleep_for(std::chrono::milliseconds(180));
  assert(sim.status().state.simulationTime==time);
  assert(display->timed==display->submitted); // paused camera redraw and pending frames drained
  auto done=display->submitted.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(90));assert(display->submitted==done);
  sim.setBrightness(.5f);until([&]{return display->submitted>done;});
  until([&]{return display->timed==display->submitted;});
  assert(sim.status().state.simulationTime==time);
  assert(sim.resume());until([&]{return sim.status().state.simulationTime>time;});
  sim.stop();assert(display->stopped==1);
  assert(sim.status().message.find("超时")==std::string::npos);
}
