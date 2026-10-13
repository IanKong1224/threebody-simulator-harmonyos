#pragma once
#include "TrajectoryJournal.h"
#include "FrameRateJournal.h"
#include "core/physics/InitialConditions.h"
#include "renderer/common/IRenderBackend.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace threebody {
// Internal G=1 units: solar mass, AU, sqrt(AU^3 / GM_sun).
// Spherical-star exterior gravity and finite contact radii; no stellar
// evolution.
std::string validateInitialState(const ThreeBodyState<double> &state);
void advanceSimulation(ThreeBodyState<double> &state, double duration);
void advanceSimulationRK45(ThreeBodyState<double>& state,double duration);
struct SimulationStatus {
  bool running{false}, available{false};
  double gpuMilliseconds{-1};
  double actualDisplayFps{-1};
  double fps{0}, presentationFps{0}, simulationDaysPerSecond{0};
  std::string frameGenerationMessage{"超帧关闭"};
  bool hasRecording{false}, hasFrameRateRecording{false};
  std::string frameRateRecordingMessage;
  uint64_t countedRealFrames{0}, countedDisplayFrames{0};
  std::string recordingMessage;
  std::string accelerationMessage{"开始模拟后检测加速状态"};
  uint64_t presentedFrames{0};
  ThreeBodyState<double> state{};
  std::string message{"请选择初始状态并开始"};
};
class Simulator {
public:
  static Simulator &instance();
  ~Simulator();
  bool start(const ThreeBodyState<double> &initial,
             std::shared_ptr<IRenderBackend> backend, RenderConfig config,
             double speed = 1, bool run = true, bool continuation = false);
  void stop();
  void pause();
  bool resume();
  void setCamera(CameraState camera);
  void setBrightness(float brightness);
  void setTrail(bool visible, double duration);
  void setRecordingDirectory(const std::string &directory);
  std::string exportTrajectoryFile(bool csv);
  std::string exportFrameRateFile();
  double fitView();
  double followPlanet(bool follow);
  SimulationStatus status() const;
  std::string trajectoryJson() const;

private:
  void physicsWorker(double speed);
  std::mutex integrationMutex_;
  std::thread physicsThread_;
  double checkpointOrigin_{0};
  uint64_t checkpointTicks_{0};
  TrajectoryJournal journal_;
  FrameRateJournal frameRateJournal_;
  std::deque<ThreeBodyState<double>> history_;
  bool showTrails_{true};
  double trailDuration_{12};
  void worker(ThreeBodyState<double> state,
              std::shared_ptr<IRenderBackend> backend, RenderConfig config,
              double speed);
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::thread thread_;
  std::atomic<bool> terminate_{false}, running_{false};
  SimulationStatus status_{};
  std::vector<ThreeBodyState<double>> trajectory_;
  double recordedSpeed_{1};
  double rejectedDuration_{0};
  bool recordingFull_{false};
  CameraState camera_{};
  bool followPlanet_{false};
  float brightness_{1.f};
  uint64_t cameraRevision_{0};
};
} // namespace threebody
