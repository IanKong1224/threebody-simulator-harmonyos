#include "core/simulation/Simulator.h"
#include "renderer/common/FramePacing.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace threebody;
using Clock = std::chrono::steady_clock;
struct ImmediateDisplay : IRenderBackend {
  std::atomic<unsigned> frames{0};
  std::vector<Clock::time_point> submissions;
  bool initialize(const RenderConfig&) override { return true; }
  void shutdown() override {}
  BackendCapabilities queryCapabilities() const override { return {}; }
  FrameHandle submitFrame(const RenderFrameInput&) override {
    submissions.push_back(Clock::now());
    return {++frames};
  }
  bool isFrameComplete(FrameHandle) override { return true; }
  GpuTiming getGpuTiming(FrameHandle) override { return {}; }
  void resizePreview(uint32_t, uint32_t) override {}
};
void measure(unsigned target) {
  auto& sim = Simulator::instance();
  auto initial = makeFigureEightDouble();
  RenderConfig config;
  config.targetFps = target;
  auto display = std::make_shared<ImmediateDisplay>();
  auto begin = Clock::now();
  if (!sim.start(initial, display, config, 1)) throw std::runtime_error("start");
  while (sim.status().state.simulationTime < .8 || display->frames < 20) {
    if (Clock::now() - begin > std::chrono::seconds(15)) throw std::runtime_error("no progress");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  sim.pause();
  auto state = sim.status().state;
  sim.stop(); // Join the writer before inspecting submission timestamps.
  auto expected = initial;
  unsigned ticks = unsigned(std::llround(state.simulationTime / .01));
  for (unsigned i = 0; i < ticks; ++i) {
    double t = (i + 1) * .01;
    advanceSimulation(expected, t - expected.simulationTime);
    expected.simulationTime = t;
  }
  for (int i = 0; i < 3; ++i) {
    if (norm(expected.body[i].position - state.body[i].position) != 0 ||
        norm(expected.body[i].velocity - state.body[i].velocity) != 0)
      throw std::runtime_error("frame target changed physics");
  }
  // Inspect all but the final interval: pause may interrupt that final wait.
  // Late wakes are allowed; submitting faster than the target is not.
  for (size_t i = 1; i + 1 < display->submissions.size(); ++i) {
    if (display->submissions[i] - display->submissions[i - 1] +
            std::chrono::microseconds(100) < framePacingInterval(target))
      throw std::runtime_error("frame submitted before its pacing deadline");
  }
  std::cout << target << " target: " << display->frames << " frames, physics t="
            << state.simulationTime << "\n";
}
int main() {
  // Exact policy is tested independently of the host's real-time performance.
  if (framePacingInterval(60).count() != 16666666 ||
      framePacingInterval(120).count() != 8333333 ||
      framePacingInterval(60, true).count() != 33333333 ||
      framePacingInterval(120, true).count() != 16666666)
    throw std::runtime_error("incorrect native/generated pacing policy");
  measure(60);
  measure(120);
}
