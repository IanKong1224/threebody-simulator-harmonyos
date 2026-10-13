#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/simulation/Simulator.h"
#include "core/simulation/StellarUnits.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <limits>
#include <thread>
using namespace threebody;
struct Display final : IRenderBackend {
  std::atomic<unsigned> frames{0}, stopped{0};
  bool initialize(const RenderConfig &c) override { return !c.enableGpuTiming; }
  void shutdown() override { ++stopped; }
  BackendCapabilities queryCapabilities() const override {
    return {GraphicsBackendType::Null, true, false, true};
  }
  FrameHandle submitFrame(const RenderFrameInput &) override {
    return {frames + 1};
  }
  bool isFrameComplete(FrameHandle) override { return true; }
  GpuTiming getGpuTiming(FrameHandle) override {
    assert(false);
    return {};
  }
  void presentFrame(FrameHandle) override { ++frames; }
  void resizePreview(uint32_t, uint32_t) override {}
};
template <class F> void waitFor(F predicate) {
  for (unsigned i = 0; i < 2000; ++i) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(false);
}
int main() {
  assert(std::abs(stellar::timeDays() - 58.13244) < .0001);
  assert(std::abs(stellar::velocityKmS() - 29.78469) < .0001);
  assert(std::abs(stellar::contactAU(1, 1) - .0093009345) < 1e-9);
  auto physical = makeFigureEightDouble();
  for (const auto &body : physical.body) {
    const double kmS = body.velocity.x * stellar::velocityKmS();
    assert(std::abs(kmS / stellar::velocityKmS() - body.velocity.x) < 1e-15);
  }
  // One AU between two solar masses: numerical acceleration must match GM/r² in SI.
  auto original = physical;
  const double dt = 1e-7;
  advanceSimulation(physical, dt);
  Vec3<double> acceleration{};
  for (unsigned j = 1; j < 3; ++j) {
    auto displacement = (original.body[j].position - original.body[0].position) * stellar::auMeters;
    double r = norm(displacement);
    acceleration += displacement * (stellar::solarGM * original.body[j].mass / (r*r*r));
  }
  auto numerical = (physical.body[0].velocity - original.body[0].velocity) *
      (stellar::velocityKmS() * 1000 / (dt * stellar::timeSeconds()));
  assert(norm(numerical - acceleration) / norm(acceleration) < 1e-5);
  auto contact = original;
  contact.body[0].position = contact.body[1].position + Vec3<double>{.007, 0, 0};
  assert(!validateInitialState(contact).empty());
  bool touching = false;
  try { advanceSimulation(contact, .001); } catch (...) { touching = true; }
  assert(touching && contact.simulationTime == 0);
  contact.body[0].position = contact.body[1].position + Vec3<double>{.012, 0, 0};
  assert(validateInitialState(contact).empty());

  auto a = makeFigureEightDouble();
  assert(validateInitialState(a).empty());
  auto b = a;
  b.body[0].mass = -1;
  assert(!validateInitialState(b).empty());
  b = a;
  b.body[0].position = b.body[1].position;
  assert(!validateInitialState(b).empty());
  b = a;
  b.body[1].velocity.x = std::numeric_limits<double>::quiet_NaN();
  assert(!validateInitialState(b).empty());
  b = a;
  b.body[0].mass = 1.2;
  advanceSimulation(a, .01);
  advanceSimulation(b, .01);
  assert(norm(a.body[1].position - b.body[1].position) > 1e-6);
  a = makeFigureEightDouble();
  b = a;
  advanceSimulation(a, .05);
  for (int i = 0; i < 5; ++i)
    advanceSimulation(b, .01);
  assert(norm(a.body[0].position - b.body[0].position) < 1e-11);
  b = a;
  b.body[0].position = b.body[1].position;
  auto before = b;
  bool rejected = false;
  try {
    advanceSimulation(b, .01);
  } catch (...) {
    rejected = true;
  }
  assert(rejected && b.simulationTime == before.simulationTime);
  auto &sim = Simulator::instance();
  auto display = std::make_shared<Display>();
  RenderConfig c;
  c.framesInFlight = 1;
  c.enableGpuTiming = false;
  assert(sim.start(makeAsymmetricDemo(), display, c));
  // A shared CI runner may miss timer deadlines; await progress instead of
  // assuming that 0.5 wall seconds imply 0.3 simulation seconds.
  waitFor([&] { auto s = sim.status(); return s.fps > 0 && s.state.simulationTime > .3; });
  auto live = sim.status();
  assert(live.fps > 0 && live.fps < 65 && live.state.simulationTime > .3);
  sim.pause();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto paused = sim.status();
  assert(paused.fps == 0 && !paused.running);
  const auto recorded = sim.trajectoryJson();
  assert(recorded.find("threebody-trajectory-v1") != std::string::npos);
  std::ofstream("/tmp/threebody-recording-test.json") << recorded;
  auto frames = display->frames.load();
  sim.setCamera({.2f, .3f, 6});
  waitFor([&] { return display->frames > frames; });
  assert(sim.status().state.simulationTime == paused.state.simulationTime);
  frames = display->frames.load();
  sim.setBrightness(.1f);
  waitFor([&] { return display->frames > frames; });
  assert(sim.status().state.simulationTime == paused.state.simulationTime);
  assert(sim.trajectoryJson() == recorded); // Paused redraws never duplicate samples.
  sim.setBrightness(1.f);
  assert(sim.resume());
  waitFor([&] {
    return sim.status().state.simulationTime > paused.state.simulationTime + .1;
  });
  sim.stop();
  assert(display->stopped == 1 && sim.status().fps == 0);
  auto restarted = std::make_shared<Display>();
  assert(sim.start(makeFigureEightDouble(), restarted, c, 1, false));
  waitFor([&] { return restarted->frames > 0; });
  assert(sim.status().state.simulationTime == 0 && !sim.status().running);
  assert(sim.trajectoryJson().find("\"samples\":[[0,") != std::string::npos);
  sim.stop();
  auto beyondMinute = makeFigureEightDouble();
  beyondMinute.simulationTime = 59.9;
  auto continuous = std::make_shared<Display>();
  assert(sim.start(beyondMinute, continuous, c, 4, true));
  waitFor([&] { return sim.status().state.simulationTime > 60.5; });
  assert(sim.status().running && continuous->frames > 0);
  sim.stop();
  std::cout << "PASS simulator: parameter validation, mass affects motion, "
               "frame-independent integration, collision rollback, "
               "presentation FPS, pause/redraw/resume/reset\n";
}
