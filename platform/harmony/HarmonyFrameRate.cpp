#include "HarmonyFrameRate.h"
#include "renderer/common/DisplayTickGate.h"
#include <native_vsync/native_vsync.h>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <hilog/log.h>
namespace threebody {
namespace {
struct State {
  std::mutex mutex;
  std::condition_variable tick;
  DisplayTickGate gate;
  static uint64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  OH_NativeVSync* vsync{};
  uint64_t windowId{};
  bool active{};
  int rangeResult{-1}, requestResult{-1};
  static void callback(long long,void* context) {
    auto& s=*static_cast<State*>(context);
    std::lock_guard<std::mutex> lock(s.mutex);
    if(s.active && s.vsync) {
      long long period=0;
      OH_NativeVSync_GetPeriod(s.vsync,&period);
      s.gate.signal(nowNs(),period>0?static_cast<uint64_t>(period):0);
      s.tick.notify_all();
      s.requestResult=OH_NativeVSync_RequestFrame(s.vsync,callback,&s);
    }
  }
};
// Stable callback context for the process lifetime. Destroy the native connection
// outside its callback mutex, after marking it inactive.
State& state(){static State* s=new State;return *s;}
}
void configureWindowFrameRate(uint64_t windowId) {
  auto& s=state();OH_NativeVSync* old;
  {std::lock_guard<std::mutex> lock(s.mutex);if(s.windowId==windowId)return;s.active=false;s.gate.reset();old=s.vsync;s.vsync=nullptr;s.windowId=windowId;}
  if(old)OH_NativeVSync_Destroy(old);
  std::lock_guard<std::mutex> lock(s.mutex);
  if(windowId)s.vsync=OH_NativeVSync_Create_ForAssociatedWindow(windowId,"ThreeBodyOutput",15);
}
void setWindowFrameRateActive(bool active) {
  auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);
  if(!s.vsync || s.active==active)return;
#ifdef THREEBODY_BASIC
  const int target=60;
#else
  const int target=120;
#endif
  OH_NativeVSync_ExpectedRateRange range{0,active?target:0,active?target:0};
  s.rangeResult=OH_NativeVSync_SetExpectedFrameRateRange(s.vsync,&range);
  s.gate.reset();
  s.active=active;
  if(active)s.requestResult=OH_NativeVSync_RequestFrame(s.vsync,State::callback,&s);
  OH_LOG_Print(LOG_APP,LOG_INFO,0x3200,"ThreeBodySimulator","Window VSync target=%{public}d window=%{public}llu range=%{public}d request=%{public}d",active?target:0,(unsigned long long)s.windowId,s.rangeResult,s.requestResult);
}
bool waitForWindowDisplayTick(const std::function<bool()>& cancelled) {
  auto& s=state();std::unique_lock<std::mutex> lock(s.mutex);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
  while(!cancelled()) {
    if(s.active && s.gate.consume(State::nowNs()))return true;
    if(std::chrono::steady_clock::now()>=deadline)return false;
    s.tick.wait_for(lock,std::chrono::milliseconds(10));
  }
  return false;
}
std::string windowFrameRateStatus(){auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);return "窗口 VSync："+std::string(s.active?"申请中":"未运行")+" range="+std::to_string(s.rangeResult)+" request="+std::to_string(s.requestResult);}
}
