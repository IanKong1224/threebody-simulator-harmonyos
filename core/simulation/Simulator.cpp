#include "Simulator.h"
#include "StellarUnits.h"
#include "RestrictedPlanet.h"
#include "core/physics/RK45Integrator.h"
#include "core/physics/RK4Integrator.h"
#include "renderer/common/FramePacing.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace threebody {
namespace {
bool finite(Vec3<double> v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
void checkState(const ThreeBodyState<double> &s) {
  for (auto &b : s.body)
    if (!finite(b.position) || !finite(b.velocity))
      throw std::runtime_error("数值超出范围，请调整初始状态后重新开始");
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = i + 1; j < 3; ++j)
      if (norm(s.body[i].position - s.body[j].position) <=
          stellar::contactAU(s.body[i].mass, s.body[j].mass))
        throw std::runtime_error(
            "恒星表面接触，已暂停在接触前；当前模型尚不模拟碰撞过程");
}
} // namespace
std::string validateInitialState(const ThreeBodyState<double> &s) {
  for (auto &b : s.body) {
    if (!std::isfinite(b.mass) || b.mass < .8 || b.mass > 1.2)
      return "类太阳模型质量须为 0.8～1.2 倍太阳质量";
    if (!finite(b.position) ||
        std::max({std::abs(b.position.x), std::abs(b.position.y),
                  std::abs(b.position.z)}) > 100)
      return "位置各分量须为 -100～100 AU";
    if (!finite(b.velocity) ||
        std::max({std::abs(b.velocity.x), std::abs(b.velocity.y),
                  std::abs(b.velocity.z)}) > 20)
      return "速度各分量须约为 -595.69～595.69 km/s";
  }
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = i + 1; j < 3; ++j)
      if (norm(s.body[i].position - s.body[j].position) <=
          stellar::contactAU(s.body[i].mass, s.body[j].mass))
        return "初始恒星表面不能接触或重叠，请增大间距";
  return "";
}
void advanceSimulation(ThreeBodyState<double> &s, double duration) {
  if (!std::isfinite(duration) || duration < 0 || duration > .201)
    throw std::runtime_error("Invalid simulation interval");
  auto next = s;
  RK4Integrator<double> rk;
  double remaining = duration;
  unsigned steps = 0;
  while (remaining > 1e-12) {
    checkState(next);
    double dt = std::min(remaining, 1e-4);
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned j = i + 1; j < 3; ++j) {
        double r = norm(next.body[i].position - next.body[j].position);
        double v = norm(next.body[i].velocity - next.body[j].velocity);
        dt = std::min(dt,
                      .01 * std::sqrt(r * r * r /
                                      (next.body[i].mass + next.body[j].mass)));
        if (v > 0)
          dt = std::min(dt, .01 * r / v);
      }
    // A short final checkpoint remainder is not a close-encounter failure.
    // Still reject when the encounter limit, rather than the remainder, is tiny.
    if (++steps > 10000 || (dt < 1e-10 && dt < remaining))
      throw std::runtime_error(
          "近距离运动需要更小时间步，已暂停；请调整初始状态");
    rk.step(next, dt);
    remaining -= dt;
  }
  checkState(next);
  s = next; // A rejected interval never publishes a partially advanced state.
}
void advanceSimulationRK45(ThreeBodyState<double>& s,double duration) {
  if(!std::isfinite(duration)||duration<0||duration>.201)throw std::runtime_error("Invalid simulation interval");
  auto next=s;double remaining=duration,h=std::min(duration,.001);unsigned trials=0,planetTrials=0;
  while(remaining>1e-12){
    checkState(next);h=std::min(h,remaining);
    for(int i=0;i<3;++i)for(int j=i+1;j<3;++j){
      const double r=norm(next.body[i].position-next.body[j].position),v=norm(next.body[i].velocity-next.body[j].velocity);
      h=std::min(h,.01*std::sqrt(r*r*r/(next.body[i].mass+next.body[j].mass)));
      if(v>0)h=std::min(h,.01*r/v);
    }
    if(++trials>10000||(h<1e-10&&h<remaining))throw std::runtime_error("RK45 精度要求超出当前计算范围，已暂停");
    auto trial=rk45Trial(next,h);
    if(trial.error<=1){checkState(trial.state);advancePlanet(next,trial.state,1e-10,1e-12,&planetTrials);next=trial.state;remaining-=h;}
    const double factor=trial.error>0 ? std::max(.2,std::min(5.,.9*std::pow(trial.error,-.2))) : 5.;
    h=std::min(.001,h*factor);
  }
  checkState(next);s=next;
}
Simulator &Simulator::instance() {
  static Simulator s;
  return s;
}
Simulator::~Simulator() { stop(); }
bool Simulator::start(const ThreeBodyState<double> &initial,
                      std::shared_ptr<IRenderBackend> backend,
                      RenderConfig config, double speed, bool run,
                      bool continuation) {
  if (config.targetFps != 60 && config.targetFps != 120) return false;
  if (!backend || !std::isfinite(speed) || speed <= 0 || speed > 4)
    return false;
  stop();
  if (!continuation) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = {};
    trajectory_.clear();
    trajectory_.reserve(36001);
    trajectory_.push_back(initial);
    history_.clear();
    history_.push_back(initial);
    recordedSpeed_ = speed;
    rejectedDuration_ = 0;
    recordingFull_ = false;
    status_.state = initial;
    checkpointOrigin_ = initial.simulationTime;
    checkpointTicks_ = 0;
    std::ostringstream meta;
    meta.imbue(std::locale::classic());
    meta << std::setprecision(17);
    meta << "{\"schema\":\"threebody-trajectory-v2\",\"model\":\"solar-scale-"
            "v1\","
            "\"variant\":\""
         << (initial.planetEnabled ? "planet-experiment-v1" : config.visualStyle == VisualStyle::Basic ? "basic-v1"
                                                      : (config.visualStyle == VisualStyle::Particles ? "particles-v1" : "detailed-rk45-v1"))
         << "\","
#ifdef THREEBODY_DETAILED
            "\"integrator\":\"Dormand-Prince-5(4)-double\",\"maxStep\":0.001,"
            "\"relativeTolerance\":1e-10,\"absoluteTolerance\":1e-12,\"forceEvaluationsPerTrial\":7,"
#else
   #ifdef THREEBODY_DETAILED
         "\"integrator\":\"Dormand-Prince-5(4)-double\",\"maxStep\":0.001,\"relativeTolerance\":1e-10,\"absoluteTolerance\":1e-12,"
#else
         "\"integrator\":\"RK4-double-distance-adaptive\",\"maxStep\":0.0001,"
#endif
#endif
            "\"encounterFactor\":0.01,"
            "\"sampleInterval\":0.01,\"samplePolicy\":\"initial plus fixed "
            "simulation-time checkpoints; independent of rendering\","
            "\"physicsVersion\":\"fixed-checkpoints-integrators-v3\",\"minStep\":1e-10,"
            "\"maxSubstepsPerCheckpoint\":10000,"
            "\"contactPolicy\":\"pause before contact; rollback rejected checkpoint\","
            "\"precision\":\"float64\",\"G\":1,\"solarGM\":"
         << stellar::solarGM << ",\"auMeters\":" << stellar::auMeters
         << ",\"solarRadiusMeters\":" << stellar::solarRadiusMeters
         << ",\"radiusMassExponent\":0.8,\"timeUnitSeconds\":"
         << stellar::timeSeconds()
         << ",\"velocityUnitKmS\":" << stellar::velocityKmS()
         << ",\"speed\":" << speed << ",\"width\":" << config.width
         << ",\"height\":" << config.height
         << ",\"targetRenderFps\":" << config.targetFps
         << ",\"backend\":" << int(backend->queryCapabilities().type)
         << ",\"columns\":[\"time_internal\"";
    for (int i = 1; i <= 3; ++i)
      for (const char *field : {"mass_solar", "x_AU", "y_AU", "z_AU",
                                "vx_internal", "vy_internal", "vz_internal"})
        meta << ",\"star" << i << "_" << field << "\"";
    if(initial.planetEnabled) {
      for(const char* field : {"mass_solar","x_AU","y_AU","z_AU","vx_internal","vy_internal","vz_internal"})
        meta << ",\"planet_" << field << "\"";
    }
    meta << "]";
    if(initial.planetEnabled)meta << ",\"planetEnabled\":true,\"restrictedModel\":\"three massive stars + test particle; no back-reaction\",\"planetIntegrator\":\"DP5(4); rtol=1e-10 atol=1e-12; cubic Hermite stellar interpolation per accepted step\",\"planetRadiusAU\":" << initial.planetRadius << ",\"planetDisplayRadiusAU\":0.07";
    meta << "}";
    journal_.begin(meta.str(), initial);
  }
  {
    std::lock_guard<std::mutex> l(mutex_);
    status_.message = "正在准备画面";
    status_.running = run;
  }
  terminate_ = false;
  running_ = run;
  if(config.recordFrameRates) frameRateJournal_.start(!continuation, [this]{
    std::lock_guard<std::mutex> l(mutex_);
    return FrameRateSnapshot{status_.countedRealFrames,status_.countedDisplayFrames,
      status_.state.simulationTime,bool(running_),status_.accelerationMessage,status_.frameGenerationMessage,running_ ? status_.actualDisplayFps : -1};
  });
  thread_ =
      std::thread(&Simulator::worker, this, initial, backend, config, speed);
  physicsThread_ = std::thread(&Simulator::physicsWorker, this, speed);
  return true;
}
void Simulator::stop() {
  frameRateJournal_.stop();
  const bool hadWorker = thread_.joinable();
  terminate_ = true;
  running_ = false;
  wake_.notify_all();
  if (physicsThread_.joinable())
    physicsThread_.join();
  if (thread_.joinable())
    thread_.join();
  if (hadWorker) {
    std::lock_guard<std::mutex> l(mutex_);
    journal_.setReason(status_.message == "运行中" ? "模拟已停止"
                                                   : status_.message);
  }
  journal_.flush();
  std::lock_guard<std::mutex> l(mutex_);
  status_.running = false;
  status_.available = false;
  status_.fps = 0; status_.presentationFps = 0;
}
void Simulator::pause() {
  running_ = false;
  std::lock_guard<std::mutex> integration(integrationMutex_);
  std::lock_guard<std::mutex> l(mutex_);
  status_.running = false;
  status_.fps = 0; status_.presentationFps = 0;
  status_.simulationDaysPerSecond = 0;
  ++cameraRevision_;
  if (status_.message == "运行中")
    status_.message = "已暂停";
  if (status_.available)
    journal_.setReason(status_.message);
  wake_.notify_all();
}
bool Simulator::resume() {
  std::lock_guard<std::mutex> l(mutex_);
  if (!status_.available || terminate_ || rejectedDuration_ > 0)
    return false;
  running_ = true;
  status_.running = true;
  status_.message = "运行中";
  journal_.setReason(status_.message);
  wake_.notify_all();
  return true;
}
void Simulator::setCamera(CameraState c) {
  std::lock_guard<std::mutex> l(mutex_);
  c.target = camera_.target;
  c.minimumDistance = followPlanet_ ? .18f : 2.f;
  camera_ = boundedCamera(c);
  ++cameraRevision_;
  wake_.notify_all();
}
void Simulator::setBrightness(float value) {
  if (!std::isfinite(value))
    return;
  std::lock_guard<std::mutex> l(mutex_);
  brightness_ = std::max(0.f, std::min(2.f, value));
  ++cameraRevision_; // Wake a paused renderer without advancing physics.
  wake_.notify_all();
}
double Simulator::followPlanet(bool follow) {
  std::lock_guard<std::mutex> l(mutex_);
  followPlanet_=follow && status_.state.planetEnabled;
  camera_.minimumDistance=followPlanet_?.18f:2.f;
  if(followPlanet_)camera_.distance=.5f;
  ++cameraRevision_;wake_.notify_all();return camera_.distance;
}
double Simulator::fitView() {
  std::lock_guard<std::mutex> l(mutex_);
  followPlanet_=false;
  camera_.minimumDistance=2.f;
  Vec3<double> center{};
  for (const auto &b : status_.state.body)
    center += b.position / 3.0;
  double radius = 0;
  for (const auto &b : status_.state.body)
    radius = std::max(radius, norm(b.position - center));
  if(status_.state.planetEnabled)radius=std::max(radius,norm(status_.state.planet.position-center));
  camera_.target = {float(center.x), float(center.y), float(center.z)};
  camera_.distance = float(std::min(500.0, std::max(5.0, radius * 3.5)));
  ++cameraRevision_;
  wake_.notify_all();
  return camera_.distance;
}
SimulationStatus Simulator::status() const {
  SimulationStatus result;
  {std::lock_guard<std::mutex> l(mutex_);result=status_;}
  result.hasFrameRateRecording = frameRateJournal_.hasRecording();
  result.frameRateRecordingMessage = frameRateJournal_.message();
  result.hasRecording = journal_.hasRecording();
  result.recordingMessage = journal_.message();
  return result;
}
std::string Simulator::trajectoryJson() const {
  std::lock_guard<std::mutex> l(mutex_);
  if (trajectory_.empty())
    return "";
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17);
  out << "{\"schema\":\"threebody-trajectory-v1\",\"model\":\"solar-scale-v1\","
#ifdef THREEBODY_DETAILED
         "\"integrator\":\"Dormand-Prince-5(4)-double\",\"maxStep\":0.001,\"relativeTolerance\":1e-10,\"absoluteTolerance\":1e-12,"
#else
         "\"integrator\":\"RK4-double-distance-adaptive\",\"maxStep\":0.0001,"
#endif
         "\"encounterFactor\":0.01,\"G\":1,\"solarGM\":"
      << stellar::solarGM << ",\"auMeters\":" << stellar::auMeters
      << ",\"solarRadiusMeters\":" << stellar::solarRadiusMeters
      << ",\"radiusMassExponent\":0.8,\"timeUnitSeconds\":"
      << stellar::timeSeconds()
      << ",\"velocityUnitKmS\":" << stellar::velocityKmS()
      << ",\"planetEnabled\":" << (status_.state.planetEnabled?"true":"false")
      << ",\"speed\":" << recordedSpeed_
      << ",\"truncated\":" << (recordingFull_ ? "true" : "false")
      << ",\"rejectedInterval\":" << rejectedDuration_
      << ",\"currentInternalTime\":" << status_.state.simulationTime
      << ",\"samplePolicy\":\"initial plus fixed 0.01 simulation-time "
         "checkpoints; no paused redraws\","
         "\"columns\":[\"internalTime\",\"massSolar,xAU,yAU,zAU,vxInternal,"
         "vyInternal,vzInternal (star1,star2,star3)\"";
  if(status_.state.planetEnabled)out << ",\"planet: massSolar,xAU,yAU,zAU,vxInternal,vyInternal,vzInternal\"";
  out << "],\"samples\":[";
  bool first = true;
  for (const auto &s : trajectory_) {
    if (!first)
      out << ',';
    first = false;
    out << '[' << s.simulationTime;
    for (const auto &b : s.body)
      out << ',' << b.mass << ',' << b.position.x << ',' << b.position.y << ','
          << b.position.z << ',' << b.velocity.x << ',' << b.velocity.y << ','
          << b.velocity.z;
    if(s.planetEnabled){const auto& b=s.planet;out << ',' << b.mass << ',' << b.position.x << ',' << b.position.y << ',' << b.position.z << ',' << b.velocity.x << ',' << b.velocity.y << ',' << b.velocity.z;}
    out << ']';
  }
  out << "]}";
  return out.str();
}

void Simulator::setTrail(bool visible, double duration) {
  std::lock_guard<std::mutex> l(mutex_);
  showTrails_ = visible;
  trailDuration_ = std::max(1.0, std::min(18.0, duration));
  ++cameraRevision_;
  wake_.notify_all();
}
void Simulator::setRecordingDirectory(const std::string &directory) {
  journal_.configure(directory);
  frameRateJournal_.configure(directory);
}
std::string Simulator::exportFrameRateFile() {
  pause();
  return frameRateJournal_.exportFile();
}
std::string Simulator::exportTrajectoryFile(bool csv) {
  pause();
  return journal_.exportFile(csv);
}
void Simulator::physicsWorker(double speed) {
  using Clock = std::chrono::steady_clock;
  double origin;
  {
    std::lock_guard<std::mutex> l(mutex_);
    origin = checkpointOrigin_;
  }
  uint64_t ticks;
  { std::lock_guard<std::mutex> l(mutex_); ticks = checkpointTicks_; }
  auto rateStart = Clock::now();
  double rateOrigin = origin;
  bool wasRunning = false;
  while (!terminate_) {
    {
      std::unique_lock<std::mutex> l(mutex_);
      wake_.wait(l,
                 [&] { return terminate_ || (running_ && status_.available); });
      if (terminate_)
        break;
    }
    auto begin = Clock::now();
    if (!wasRunning) {
      rateStart = begin;
      std::lock_guard<std::mutex> l(mutex_);
      rateOrigin = status_.state.simulationTime;
    }
    {
      std::lock_guard<std::mutex> integration(integrationMutex_);
      if (terminate_ || !running_) {
        wasRunning = false;
        continue;
      }
      ThreeBodyState<double> state;
      {
        std::lock_guard<std::mutex> l(mutex_);
        state = status_.state;
      }
      try {
        const double target = origin + double(ticks + 1) * .01;
#ifdef THREEBODY_DETAILED
        advanceSimulationRK45(state, target - state.simulationTime);
#else
        advanceSimulation(state, target - state.simulationTime);
#endif
        state.simulationTime = target;
        std::lock_guard<std::mutex> l(mutex_);
        // A renderer failure or pause may occur while this step is computing.
        // Do not publish a late checkpoint or overwrite its stop reason.
        if (terminate_ || !running_) continue;
        ++ticks;
        journal_.append(state);
        status_.state = state;
        checkpointTicks_ = ticks;
        status_.message = "运行中";
        history_.push_back(state);
        if (history_.size() > 1801)
          history_.pop_front();
        if (trajectory_.size() < 36001)
          trajectory_.push_back(state);
        else
          recordingFull_ = true;
        const double elapsed =
            std::chrono::duration<double>(Clock::now() - rateStart).count();
        if (elapsed >= .5) {
          status_.simulationDaysPerSecond =
              (state.simulationTime - rateOrigin) * stellar::timeDays() /
              elapsed;
          rateStart = Clock::now();
          rateOrigin = state.simulationTime;
        }
      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> l(mutex_);
        if (terminate_) break;
        running_ = false;
        status_.message = e.what();
        status_.running = false;
        status_.fps = 0; status_.presentationFps = 0;
        status_.simulationDaysPerSecond = 0;
        rejectedDuration_ = .01;
        ++cameraRevision_;
        journal_.setReason(status_.message);
      }
    }
    wake_.notify_all();
    wasRunning = running_;
    std::unique_lock<std::mutex> l(mutex_);
    wake_.wait_until(l,
                     begin + std::chrono::duration_cast<Clock::duration>(
                                 std::chrono::duration<double>(.01 / speed)),
                     [&] { return terminate_ || !running_; });
    if (!running_)
      wasRunning = false;
  }
}
void Simulator::worker(ThreeBodyState<double>,
                       std::shared_ptr<IRenderBackend> backend,
                       RenderConfig config, double) {
  using Clock = std::chrono::steady_clock;
  try {
    if (!backend->initialize(config))
      throw std::runtime_error("渲染器初始化失败");
    {
      std::lock_guard<std::mutex> l(mutex_);
      status_.available = true;
      status_.accelerationMessage = backend->accelerationStatus();
      status_.message = running_ ? "运行中" : "已暂停";
    }
    wake_.notify_all();
    uint64_t sequence = 0, seen = 0, frames = 0, presentations = 0;
    bool first = true, wasRunning = false;
    double fps = 0, presentationFps = 0;
    auto fpsStart = Clock::now();
    const bool queued = backend->queryCapabilities().supportsQueuedPresentation;
    struct PendingFrame { FrameHandle handle; bool advancing; Clock::time_point submitted; bool generated; };
    std::deque<PendingFrame> pending;
    auto retire = [&](bool wait) {
      if (pending.empty()) return;
      const auto oldest = pending.front();
      while (!backend->isPresentationComplete(oldest.handle)) {
        if (!wait || terminate_) return;
        if (Clock::now() - oldest.submitted > std::chrono::seconds(3))
          throw std::runtime_error("送显等待超时");
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      const auto timing = config.enableGpuTiming && backend->queryCapabilities().supportsGpuTiming
                              ? backend->getGpuTiming(oldest.handle) : GpuTiming{};
      if (oldest.advancing && running_) {
        ++frames;
        presentations += oldest.generated ? 2 : 1;
        const double elapsed = std::chrono::duration<double>(Clock::now() - fpsStart).count();
        if (elapsed >= .5) { fps = frames / elapsed; presentationFps = presentations / elapsed; frames = presentations = 0; fpsStart = Clock::now(); }
      }
      const auto displayTiming = backend->pollDisplayTiming();
      {
        std::lock_guard<std::mutex> l(mutex_);
        status_.actualDisplayFps = running_ ? displayTiming.fps : -1;
        status_.running = running_;
        status_.fps = running_ ? fps : 0;
        status_.presentationFps = running_ ? presentationFps : 0;
        if(oldest.advancing && running_){
          ++status_.countedRealFrames;
          status_.countedDisplayFrames += oldest.generated ? 2 : 1;
        }
        status_.frameGenerationMessage = backend->frameGenerationStatus();
        status_.gpuMilliseconds = timing.valid ? timing.milliseconds : -1;
        ++status_.presentedFrames;
      }
      pending.pop_front();
    };
    while (!terminate_) {
      // Drain paused frames too, so a last camera redraw cannot be left pending.
      while (!pending.empty() && (!running_ || pending.size() >= config.framesInFlight ||
                                  backend->isPresentationComplete(pending.front().handle))) {
        retire(true);
        if (terminate_) break;
      }

      RenderFrameInput input;
      {
        std::unique_lock<std::mutex> l(mutex_);
        wake_.wait(l, [&] {
          return terminate_ || running_ || first || seen != cameraRevision_;
        });
        if (terminate_)
          break;
        input.state = status_.state;
        input.camera = camera_;
        if(followPlanet_ && input.state.planetEnabled){
          auto p=input.state.planet.position;
          input.camera.target={float(p.x),float(p.y),float(p.z)};
        }
        input.stellarBrightness = brightness_;
        input.workloadLevel = config.workloadLevel;
        input.showTrails = showTrails_;
        input.trailDuration = trailDuration_;
        input.history =
            std::make_shared<const std::vector<ThreeBodyState<double>>>(
                history_.begin(), history_.end());
        seen = cameraRevision_;
      }
      const auto begin = Clock::now();
      const bool advancing = running_;
      if (advancing && !wasRunning) {
        fpsStart = begin;
        frames = presentations = 0;
        fps = presentationFps = 0;
      }
      input.allowFrameGeneration = advancing;
      input.sequenceId = ++sequence;
      auto frame = backend->submitFrame(input);
      if (!queued) {
        while (!backend->isFrameComplete(frame)) {
          if (Clock::now() - begin > std::chrono::seconds(3))
            throw std::runtime_error("渲染等待超时");
          if (terminate_) break;
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
      }
      if (terminate_) break;
      auto awaitDisplay = [&] {
        if(config.waitForDisplayTick && running_ && !terminate_) {
          const bool tick=config.waitForDisplayTick([&]{return bool(terminate_) || !bool(running_);});
          if(!tick && running_ && !terminate_) throw std::runtime_error("系统 VSync 超时，已停止送显；请重新开始");
        }
      };
      // Prepare ahead, then use the latest unconsumed current VSync slot.
      // Ticks arriving during preparation are usable; expired ticks never queue up.
      awaitDisplay();
      if(terminate_)break;
      bool generated = running_ && advancing && backend->presentIntermediateFrame(frame);
      if(generated && config.waitForDisplayTick) {
        awaitDisplay();
      } else if(generated) {
        // Present N-0.5 before N, paced apart; never feed generated pixels to physics.
        const auto realDeadline = Clock::now() + framePacingInterval(config.targetFps);
        std::unique_lock<std::mutex> l(mutex_);
        wake_.wait_until(l, realDeadline, [&]{return terminate_ || !running_;});
      }
      // Even a pause must finish the pair so slot lifetime tracking stays valid.
      backend->presentFrame(frame);
      pending.push_back({frame, advancing, begin, generated});
      retire(!queued);
      wasRunning = running_;
      first = false;
      std::unique_lock<std::mutex> l(mutex_);
      if(!config.waitForDisplayTick) wake_.wait_until(l, begin + framePacingInterval(config.targetFps, backend->frameGenerationActive()),
                       [&] { return terminate_ || !running_; });
    }
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> l(mutex_);
    // Stop the producer before publishing the error, including during shutdown.
    terminate_ = true;
    running_ = false;
    status_.message = e.what();
    journal_.setReason(status_.message);
  }
  backend->shutdown();
  terminate_ = true;
  running_ = false;
  {
    std::lock_guard<std::mutex> l(mutex_);
    status_.running = false;
    status_.available = false;
    status_.fps = 0; status_.presentationFps = 0;
    status_.simulationDaysPerSecond = 0;
  }
  wake_.notify_all();
}
} // namespace threebody
