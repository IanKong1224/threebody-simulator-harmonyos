#include "core/simulation/Simulator.h"
#include "core/simulation/RestrictedPlanet.h"
#include "core/simulation/StellarUnits.h"
#include "platform/harmony/HarmonyRenderer.h"
#include "platform/harmony/HarmonySurfaceState.h"
#include "platform/harmony/HarmonyFrameRate.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <cmath>
#include <chrono>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <vector>
#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200
#define LOG_TAG "ThreeBodySimulator"
namespace {
// Only frame-rate negotiation runs on ArkUI; all rendering remains on the worker.
OH_NativeXComponent* frameRateComponent=nullptr;
bool frameRateActive=false;
bool frameRateAttempted=false;
int32_t frameRangeResult=-1,frameCallbackResult=-1;
uint64_t vsyncFirst=0,vsyncLast=0,vsyncIntervals=0;
double vsyncFps=-1;
std::chrono::steady_clock::time_point vsyncArrival{};
void FrameRateCallback(OH_NativeXComponent*,uint64_t timestamp,uint64_t) {
  // ArkUI callback: count only, never draw or wait here. This is NOT scanout FPS.
  vsyncArrival=std::chrono::steady_clock::now();
  if(!vsyncFirst || timestamp<=vsyncLast) {vsyncFirst=vsyncLast=timestamp;vsyncIntervals=0;return;}
  vsyncLast=timestamp; ++vsyncIntervals;
  if(timestamp-vsyncFirst>=500000000ULL) {
    vsyncFps=1e9*double(vsyncIntervals)/double(timestamp-vsyncFirst);
    vsyncFirst=timestamp;vsyncIntervals=0;
  }
}
void UpdateFrameRate(bool active) {
  threebody::setWindowFrameRateActive(active);
  if(!frameRateComponent)return;
  if(active){
    if(frameRateAttempted)return;
    frameRateAttempted=true;
#ifdef THREEBODY_BASIC
    OH_NativeXComponent_ExpectedRateRange range{60,60,60};
#else
    OH_NativeXComponent_ExpectedRateRange range{120,120,120};
#endif
    frameRangeResult=OH_NativeXComponent_SetExpectedFrameRateRange(frameRateComponent,&range);
    frameCallbackResult=OH_NativeXComponent_RegisterOnFrameCallback(frameRateComponent,FrameRateCallback);
    frameRateActive=frameCallbackResult==OH_NATIVEXCOMPONENT_RESULT_SUCCESS;
    OH_LOG_INFO(LOG_APP,"Frame rate request target=%{public}d range=%{public}d callback=%{public}d",range.expected,frameRangeResult,frameCallbackResult);
  } else {
    if(frameRateActive) OH_NativeXComponent_UnregisterOnFrameCallback(frameRateComponent);
    frameRateActive=false; frameRateAttempted=false;
    vsyncFirst=vsyncLast=vsyncIntervals=0;vsyncFps=-1;
  }
}
napi_value boolean(napi_env env, bool v) {
  napi_value x;
  napi_get_boolean(env, v, &x);
  return x;
}
void number(napi_env env, napi_value obj, const char *key, double v) {
  napi_value x;
  napi_create_double(env, v, &x);
  napi_set_named_property(env, obj, key, x);
}
void text(napi_env env, napi_value obj, const char *key, const std::string &v) {
  napi_value x;
  napi_create_string_utf8(env, v.c_str(), NAPI_AUTO_LENGTH, &x);
  napi_set_named_property(env, obj, key, x);
}
napi_value values(napi_env env, const threebody::ThreeBodyState<double> &s) {
  napi_value a;
  napi_create_array_with_length(env, 21, &a);
  unsigned i = 0;
  for (auto &b : s.body) {
    double v[] = {b.mass,
                  b.position.x,
                  b.position.y,
                  b.position.z,
                  b.velocity.x * threebody::stellar::velocityKmS(),
                  b.velocity.y * threebody::stellar::velocityKmS(),
                  b.velocity.z * threebody::stellar::velocityKmS()};
    for (double d : v) {
      napi_value x;
      napi_create_double(env, d, &x);
      napi_set_element(env, a, i++, x);
    }
  }
  return a;
}
napi_value SetOutputWindow(napi_env env,napi_callback_info info) {
  size_t n=1;napi_value arg;int64_t id=0;
  napi_get_cb_info(env,info,&n,&arg,nullptr,nullptr);
  if(n!=1 || napi_get_value_int64(env,arg,&id)!=napi_ok || id<0)return boolean(env,false);
  threebody::configureWindowFrameRate(static_cast<uint64_t>(id));return boolean(env,true);
}
napi_value Start(napi_env env, napi_callback_info info) {
  size_t argc = 5;
  napi_value args[5];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  bool gles = false, isArray = false;
  uint32_t quality = 0, length = 0;
  double speed = 1;
  if (argc < 4 || napi_get_value_bool(env, args[0], &gles) != napi_ok ||
      napi_is_array(env, args[1], &isArray) != napi_ok || !isArray ||
      napi_get_array_length(env, args[1], &length) != napi_ok || length != 21 ||
      napi_get_value_uint32(env, args[2], &quality) != napi_ok ||
      napi_get_value_double(env, args[3], &speed) != napi_ok ||
      !std::isfinite(speed) || speed <= 0 || speed > 4) {
    napi_throw_error(env, nullptr, "模拟参数格式错误");
    return nullptr;
  }
  threebody::ThreeBodyState<double> s;
  double v[21];
  for (unsigned i = 0; i < 21; ++i) {
    napi_value x;
    if (napi_get_element(env, args[1], i, &x) != napi_ok ||
        napi_get_value_double(env, x, &v[i]) != napi_ok) {
      napi_throw_error(env, nullptr, "请输入有效数字");
      return nullptr;
    }
  }
  for (unsigned i = 0; i < 3; ++i) {
    auto k = i * 7;
    s.body[i] = {{v[k + 1], v[k + 2], v[k + 3]},
                 {v[k + 4] / threebody::stellar::velocityKmS(),
                  v[k + 5] / threebody::stellar::velocityKmS(),
                  v[k + 6] / threebody::stellar::velocityKmS()},
                 v[k]};
  }
  auto error = threebody::validateInitialState(s);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
#ifdef THREEBODY_PLANET
  bool enabled=true;
  if(argc>=5 && napi_get_value_bool(env,args[4],&enabled)!=napi_ok){napi_throw_error(env,nullptr,"行星开关参数无效");return nullptr;}
  if(enabled)threebody::initializePlanet(s);
#endif
  return boolean(env,
                 threebody::startHarmonySimulation(gles, s, quality, speed));
}
napi_value Preset(napi_env env, napi_callback_info info) {
  size_t n = 1;
  napi_value a;
  uint32_t id = 0;
  napi_get_cb_info(env, info, &n, &a, nullptr, nullptr);
  if (n)
    napi_get_value_uint32(env, a, &id);
  if (id == 5) return values(env, threebody::makeReturningTriple());
  if (id == 4) return values(env, threebody::makeStableTriple());
  if (id == 3) return values(env, threebody::makeLongLivedTriple());
  return values(env, id == 1 ? threebody::makeFigureEightDouble()
                             : threebody::makeAsymmetricDemo());
}
napi_value RandomPreset(napi_env env, napi_callback_info info) {
  size_t n=1; napi_value a; uint32_t seed=0;
  napi_get_cb_info(env,info,&n,&a,nullptr,nullptr);
  if(n!=1 || napi_get_value_uint32(env,a,&seed)!=napi_ok){
    napi_throw_error(env,nullptr,"随机种子无效");return nullptr;
  }
  return values(env,threebody::makeRandomTriple(seed));
}
napi_value Pause(napi_env env, napi_callback_info) {
  threebody::Simulator::instance().pause();
  UpdateFrameRate(false);
  return boolean(env, true);
}
napi_value Resume(napi_env env, napi_callback_info) {
  return boolean(env, threebody::resumeHarmonySimulation());
}
napi_value Stop(napi_env env, napi_callback_info) {
  threebody::stopHarmonyPreview();
  UpdateFrameRate(false);
  return boolean(env, true);
}
napi_value SetCamera(napi_env env, napi_callback_info info) {
  size_t n = 3;
  napi_value a[3];
  double v[3];
  napi_get_cb_info(env, info, &n, a, nullptr, nullptr);
  if (n != 3)
    return boolean(env, false);
  for (unsigned i = 0; i < 3; ++i)
    if (napi_get_value_double(env, a[i], &v[i]) != napi_ok ||
        !std::isfinite(v[i]))
      return boolean(env, false);
  threebody::Simulator::instance().setCamera(
      {float(v[0]), float(v[1]), float(v[2])});
  return boolean(env, true);
}
napi_value IsPlanet(napi_env env,napi_callback_info) {
#ifdef THREEBODY_PLANET
  return boolean(env,true);
#else
  return boolean(env,false);
#endif
}
napi_value FollowPlanet(napi_env env,napi_callback_info info){
  size_t n=1;napi_value arg;bool follow=false;
  napi_get_cb_info(env,info,&n,&arg,nullptr,nullptr);
  if(n!=1 || napi_get_value_bool(env,arg,&follow)!=napi_ok)return nullptr;
  napi_value v;napi_create_double(env,threebody::Simulator::instance().followPlanet(follow),&v);return v;
}
napi_value IsAccelerated(napi_env env, napi_callback_info) {
#ifdef THREEBODY_ACCELERATED
  return boolean(env, true);
#else
  return boolean(env, false);
#endif
}
napi_value SetAcceleration(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; bool enabled = false;
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc != 1 || napi_get_value_bool(env, args[0], &enabled) != napi_ok) {
    napi_throw_error(env, nullptr, "加速开关参数无效"); return nullptr;
  }
  return boolean(env, threebody::setHarmonyAcceleration(enabled));
}
napi_value SetFrameGeneration(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; bool enabled = false;
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc != 1 || napi_get_value_bool(env, args[0], &enabled) != napi_ok) {
    napi_throw_error(env, nullptr, "加速开关参数无效"); return nullptr;
  }
  return boolean(env, threebody::setHarmonyFrameGeneration(enabled));
}
napi_value SetBrightness(napi_env env, napi_callback_info info) {
  size_t n = 1;
  napi_value a;
  double value;
  napi_get_cb_info(env, info, &n, &a, nullptr, nullptr);
  if (n != 1 || napi_get_value_double(env, a, &value) != napi_ok ||
      !std::isfinite(value) || value < 0 || value > 2)
    return boolean(env, false);
  threebody::Simulator::instance().setBrightness(float(value));
  return boolean(env, true);
}
napi_value FitView(napi_env env, napi_callback_info) {
  napi_value v;
  napi_create_double(env, threebody::Simulator::instance().fitView(), &v);
  return v;
}
napi_value Trajectory(napi_env env, napi_callback_info) {
  auto json = threebody::Simulator::instance().trajectoryJson();
  napi_value value;
  napi_create_string_utf8(env, json.c_str(), json.size(), &value);
  return value;
}

napi_value IsParticles(napi_env env, napi_callback_info) {
#ifdef THREEBODY_PARTICLES
  return boolean(env,true);
#else
  return boolean(env,false);
#endif
}
napi_value IsBasic(napi_env env, napi_callback_info) {
#ifdef THREEBODY_BASIC
  return boolean(env, true);
#else
  return boolean(env, false);
#endif
}
napi_value SetTrail(napi_env env, napi_callback_info info) {
  size_t n = 2;
  napi_value args[2];
  bool visible;
  double duration;
  napi_get_cb_info(env, info, &n, args, nullptr, nullptr);
  if (n != 2 || napi_get_value_bool(env, args[0], &visible) != napi_ok ||
      napi_get_value_double(env, args[1], &duration) != napi_ok ||
      !std::isfinite(duration))
    return boolean(env, false);
  threebody::Simulator::instance().setTrail(visible, duration);
  return boolean(env, true);
}
napi_value RecordDirectory(napi_env env, napi_callback_info info) {
  size_t n = 1, length = 0;
  napi_value arg;
  napi_get_cb_info(env, info, &n, &arg, nullptr, nullptr);
  if (n != 1 ||
      napi_get_value_string_utf8(env, arg, nullptr, 0, &length) != napi_ok ||
      length > 4096)
    return boolean(env, false);
  std::vector<char> path(length + 1);
  napi_get_value_string_utf8(env, arg, path.data(), path.size(), &length);
  threebody::Simulator::instance().setRecordingDirectory(
      std::string(path.data(), length));
  return boolean(env, true);
}
struct ExportJob {
  napi_async_work work{};
  napi_deferred deferred{};
  bool csv{}, frameRate{};
  std::string path, error;
};
napi_value ExportFile(napi_env env, napi_callback_info info) {
  size_t n = 1;
  napi_value arg;
  void* kind=nullptr;
  napi_get_cb_info(env, info, &n, &arg, nullptr, &kind);
  bool csv = false;
  if (n != 1 || napi_get_value_bool(env, arg, &csv) != napi_ok)
    return nullptr;
  auto *job = new ExportJob;
  job->csv = csv;
  job->frameRate = kind != nullptr;
  napi_value promise, name;
  napi_create_promise(env, &job->deferred, &promise);
  napi_create_string_utf8(env, "TrajectoryExport", NAPI_AUTO_LENGTH, &name);
  napi_create_async_work(
      env, nullptr, name,
      [](napi_env, void *data) {
        auto *job = static_cast<ExportJob *>(data);
        try {
          job->path =
              job->frameRate ? threebody::Simulator::instance().exportFrameRateFile() :
              threebody::Simulator::instance().exportTrajectoryFile(job->csv);
        } catch (const std::exception &e) {
          job->error = e.what();
        }
      },
      [](napi_env env, napi_status status, void *data) {
        auto *job = static_cast<ExportJob *>(data);
        napi_value value;
        if (!job->error.empty() || status != napi_ok) {
          napi_value message;
          napi_create_string_utf8(
              env, job->error.empty() ? "导出任务取消" : job->error.c_str(),
              NAPI_AUTO_LENGTH, &message);
          napi_create_error(env, nullptr, message, &value);
          napi_reject_deferred(env, job->deferred, value);
        } else {
          napi_create_string_utf8(env, job->path.c_str(), NAPI_AUTO_LENGTH,
                                  &value);
          napi_resolve_deferred(env, job->deferred, value);
        }
        napi_delete_async_work(env, job->work);
        delete job;
      },
      job, &job->work);
  napi_queue_async_work(env, job->work);
  return promise;
}
napi_value Status(napi_env env, napi_callback_info) {
  auto s = threebody::Simulator::instance().status();
  napi_value obj;
  napi_create_object(env, &obj);
  napi_set_named_property(env, obj, "running", boolean(env, s.running));
  number(env, obj, "fps", s.fps);
  number(env, obj, "presentationFps", s.presentationFps);
  number(env, obj, "actualDisplayFps", s.running ? s.actualDisplayFps : -1);
  text(env, obj, "frameGenerationMessage", s.frameGenerationMessage);
  number(env, obj, "simulationDaysPerSecond", s.simulationDaysPerSecond);
  napi_set_named_property(env, obj, "hasRecording",
                          boolean(env, s.hasRecording));
  text(env, obj, "recordingMessage", s.recordingMessage);
  text(env, obj, "frameRateRecordingMessage", s.frameRateRecordingMessage);
  napi_set_named_property(env,obj,"hasFrameRateRecording",boolean(env,s.hasFrameRateRecording));
  text(env, obj, "accelerationMessage", s.accelerationMessage);
  number(env, obj, "gpuMilliseconds", s.gpuMilliseconds);
  UpdateFrameRate(s.running && s.available);
  const bool recent=std::chrono::steady_clock::now()-vsyncArrival<std::chrono::seconds(1);
  number(env,obj,"systemCallbackFps",frameRateActive && recent ? vsyncFps : -1);
  text(env,obj,"refreshRateMessage",!frameRateAttempted ? "运行时申请系统刷新率" :
    (frameRangeResult==0 && frameCallbackResult==0 ? "系统帧率申请已提交；是否达到目标以实际回调及设备监测为准" :
     "系统帧率申请失败：range="+std::to_string(frameRangeResult)+" callback="+std::to_string(frameCallbackResult)) + "；" + threebody::windowFrameRateStatus());
  number(env, obj, "time",
         s.state.simulationTime * threebody::stellar::timeDays());
  number(env, obj, "frames", s.presentedFrames);
  text(env, obj, "message", s.message);
  napi_set_named_property(env, obj, "bodies", values(env, s.state));
  return obj;
}
void OnSurfaceCreated(OH_NativeXComponent *component, void *window) {
  frameRateComponent=component;
  uint64_t width = 0;
  uint64_t height = 0;
  OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
  threebody::HarmonySurfaceState::instance().onCreated(window, width, height);
  OH_LOG_INFO(LOG_APP, "XComponent surface created %{public}llux%{public}llu",
              static_cast<unsigned long long>(width),
              static_cast<unsigned long long>(height));
}

void OnSurfaceChanged(OH_NativeXComponent *component, void *window) {
  uint64_t width = 0;
  uint64_t height = 0;
  OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
  threebody::HarmonySurfaceState::instance().onChanged(window, width, height);
  threebody::restartHarmonyPreviewIfRunning();
  OH_LOG_INFO(LOG_APP, "XComponent surface changed %{public}llux%{public}llu",
              static_cast<unsigned long long>(width),
              static_cast<unsigned long long>(height));
}

void OnSurfaceDestroyed(OH_NativeXComponent *, void *window) {
  UpdateFrameRate(false);
  frameRateComponent=nullptr;
  threebody::stopHarmonyPreview(); // Join before the native surface can become
                                   // invalid.
  threebody::HarmonySurfaceState::instance().onDestroyed(window);
  OH_LOG_INFO(LOG_APP, "XComponent surface destroyed");
}

void DispatchTouchEvent(OH_NativeXComponent *, void *) {
  // M0: native touch hook reserved. Interactive camera lands in a later
  // milestone.
}

void TryRegisterXComponent(napi_env env, napi_value exports) {
  napi_value exportInstance = nullptr;
  if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ,
                              &exportInstance) != napi_ok) {
    OH_LOG_WARN(LOG_APP, "Native module loaded without XComponent object; "
                         "control-plane APIs remain available");
    return;
  }

  OH_NativeXComponent *component = nullptr;
  if (napi_unwrap(env, exportInstance, reinterpret_cast<void **>(&component)) !=
          napi_ok ||
      component == nullptr) {
    OH_LOG_ERROR(LOG_APP, "Failed to unwrap OH_NativeXComponent");
    return;
  }

  static OH_NativeXComponent_Callback callback{};
  callback.OnSurfaceCreated = OnSurfaceCreated;
  callback.OnSurfaceChanged = OnSurfaceChanged;
  callback.OnSurfaceDestroyed = OnSurfaceDestroyed;
  callback.DispatchTouchEvent = DispatchTouchEvent;
  const int32_t rc = OH_NativeXComponent_RegisterCallback(component, &callback);
  OH_LOG_INFO(LOG_APP, "XComponent callback registration result=%{public}d",
              rc);
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor desc[] = {
      {"setOutputWindow",nullptr,SetOutputWindow,nullptr,nullptr,nullptr,napi_default,nullptr},
      {"isPlanet", nullptr, IsPlanet, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"followPlanet", nullptr, FollowPlanet, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"isAccelerated", nullptr, IsAccelerated, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"setFrameGeneration", nullptr, SetFrameGeneration, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"setAcceleration", nullptr, SetAcceleration, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"isParticles", nullptr, IsParticles, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"getRandomPreset", nullptr, RandomPreset, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"isBasic", nullptr, IsBasic, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"setTrail", nullptr, SetTrail, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"setRecordingDirectory", nullptr, RecordDirectory, nullptr, nullptr,
       nullptr, napi_default, nullptr},
      {"exportFrameRateFile", nullptr, ExportFile, nullptr, nullptr, nullptr, napi_default, reinterpret_cast<void*>(1)},
      {"exportTrajectoryFile", nullptr, ExportFile, nullptr, nullptr, nullptr,
       napi_default, nullptr},
      {"getTrajectory", nullptr, Trajectory, nullptr, nullptr, nullptr,
       napi_default, nullptr},
      {"setBrightness", nullptr, SetBrightness, nullptr, nullptr, nullptr,
       napi_default, nullptr},
      {"fitView", nullptr, FitView, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"start", nullptr, Start, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"pause", nullptr, Pause, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"resume", nullptr, Resume, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"stop", nullptr, Stop, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"setCamera", nullptr, SetCamera, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"getStatus", nullptr, Status, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"getPreset", nullptr, Preset, nullptr, nullptr, nullptr, napi_default,
       nullptr},
  };
  napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
  TryRegisterXComponent(env, exports);
  return exports;
}
EXTERN_C_END
static napi_module nativeModule = {.nm_version = 1,
                                   .nm_flags = 0,
                                   .nm_filename = nullptr,
                                   .nm_register_func = Init,
                                   .nm_modname = "entry",
                                   .nm_priv = nullptr,
                                   .reserved = {0}};
} // namespace
extern "C" __attribute__((constructor)) void RegisterEntryModule() {
  napi_module_register(&nativeModule);
}
