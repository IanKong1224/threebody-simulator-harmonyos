#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/simulation/Simulator.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
#include <unistd.h>
using namespace threebody;
using Clock=std::chrono::steady_clock;
struct Display : IRenderBackend {
  bool fail; bool active=true; bool advancing=false; bool generated=false;
  uint64_t serial=0; std::atomic<int> real{0},extra{0},stopped{0};
  explicit Display(bool f):fail(f){}
  bool initialize(const RenderConfig&)override{return true;}
  BackendCapabilities queryCapabilities()const override{return {GraphicsBackendType::Vulkan,true,false,true,true};}
  FrameHandle submitFrame(const RenderFrameInput& in)override {
    assert(!generated);advancing=in.allowFrameGeneration;++serial;
    if(fail&&serial==15)active=false;
    return {serial};
  }
  bool frameGenerationActive()const override{return active;}
  std::string frameGenerationStatus()const override{return active?"active":"fallback";}
  bool presentIntermediateFrame(FrameHandle h)override{
    assert(h.value==serial);if(!advancing||!active||serial<=3)return false;
    generated=true;++extra;return true;
  }
  void presentFrame(FrameHandle h)override{assert(h.value==serial);generated=false;++real;}
  DisplayTiming pollDisplayTiming()override{return fail ? DisplayTiming{} : DisplayTiming{true,60};}
  bool isFrameComplete(FrameHandle)override{return true;}
  GpuTiming getGpuTiming(FrameHandle)override{return {};}
  void resizePreview(uint32_t,uint32_t)override{}
  void shutdown()override{assert(!generated);++stopped;}
};
template<class F>void until(F f){auto end=Clock::now()+std::chrono::seconds(15);while(!f()){assert(Clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
int main(){
  auto& sim=Simulator::instance();
  char directory[]="/tmp/threebody-fps-sim-XXXXXX";assert(mkdtemp(directory));sim.setRecordingDirectory(directory);
  for(bool fail:{false,true}){
    auto renderer=std::make_shared<Display>(fail);RenderConfig c;c.targetFps=120;c.framesInFlight=2;c.recordFrameRates=true;
    assert(sim.start(makeFigureEightDouble(),renderer,c));
    // Wait for a complete accounting window, including after fallback. Host
    // timer resolution and shared-runner load must not act as FPS benchmarks.
    until([&]{auto s=sim.status();return s.state.simulationTime>=1.5 && s.fps>0 &&
      s.hasFrameRateRecording && (fail ? s.frameGenerationMessage=="fallback" && s.presentationFps==s.fps : s.presentationFps>s.fps*1.8);});
    auto s=sim.status();assert(s.fps>0);
    assert(s.actualDisplayFps==(fail ? -1 : 60)); // Never infer scanout from generated count.
    assert(s.hasFrameRateRecording&&s.countedRealFrames>0&&s.countedDisplayFrames>s.countedRealFrames);
    if(!fail){assert(s.presentationFps>s.fps*1.8);assert(s.presentationFps<=s.fps*2.01);}
    else {assert(s.frameGenerationMessage=="fallback");assert(s.presentationFps==s.fps);}
    sim.pause();until([&]{return !sim.status().running;});
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    auto t=sim.status().state.simulationTime;int n=renderer->extra;
    sim.setBrightness(.4f);std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(renderer->extra==n);assert(sim.status().state.simulationTime==t);
    assert(sim.status().fps==0&&sim.status().presentationFps==0);
    auto csv=sim.exportFrameRateFile();std::ifstream recorded(csv);std::string header,row;
    assert(std::getline(recorded,header)&&std::getline(recorded,row));assert(header.find("submitted_fps,render_fps")!=std::string::npos);
    auto actual=sim.status().state;auto expected=makeFigureEightDouble();
    unsigned ticks=unsigned(std::llround(actual.simulationTime/.01));
    for(unsigned k=0;k<ticks;++k){double end=(k+1)*.01;advanceSimulation(expected,end-expected.simulationTime);expected.simulationTime=end;}
    for(int k=0;k<3;++k){assert(norm(actual.body[k].position-expected.body[k].position)==0);assert(norm(actual.body[k].velocity-expected.body[k].velocity)==0);}
    assert(sim.resume());until([&]{return sim.status().state.simulationTime>t+.1;});
    sim.stop();assert(renderer->stopped==1);
    std::cout<<(fail?"fallback":"generation")<<": real="<<s.fps<<" presented="<<s.presentationFps<<"\n";
  }
}
