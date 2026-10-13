#include "core/simulation/Simulator.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace threebody;
using Clock=std::chrono::steady_clock;
struct ImmediateDisplay:IRenderBackend{
 uint64_t n=0;
 bool initialize(const RenderConfig&)override{return true;}
 void shutdown()override{}
 BackendCapabilities queryCapabilities()const override{return {};}
 FrameHandle submitFrame(const RenderFrameInput&)override{return {++n};}
 bool isFrameComplete(FrameHandle)override{return true;}
 GpuTiming getGpuTiming(FrameHandle)override{return {};}
 void resizePreview(uint32_t,uint32_t)override{}
};
double measure(unsigned target){
 auto& sim=Simulator::instance();auto initial=makeFigureEightDouble();RenderConfig config;config.targetFps=target;
 auto display=std::make_shared<ImmediateDisplay>();auto begin=Clock::now();
 if(!sim.start(initial,display,config,1))throw std::runtime_error("start");
 while(sim.status().state.simulationTime<.8){if(Clock::now()-begin>std::chrono::seconds(4))throw std::runtime_error("timeout");std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 sim.pause();auto state=sim.status().state;double elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
 sim.stop();auto expected=initial;unsigned ticks=unsigned(std::llround(state.simulationTime/.01));
 for(unsigned i=0;i<ticks;++i){double t=(i+1)*.01;advanceSimulation(expected,t-expected.simulationTime);expected.simulationTime=t;}
 for(int i=0;i<3;++i)if(norm(expected.body[i].position-state.body[i].position)!=0)throw std::runtime_error("frame target changed physics");
 const double fps=display->n/elapsed;std::cout<<target<<" target: "<<fps<<" completed frames/s, physics t="<<state.simulationTime<<"\n";return fps;
}
int main(){double slow=measure(60),fast=measure(120);if(slow>80||fast>145||fast<slow*1.5)throw std::runtime_error("pacing didn't change to 120");}
