#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/simulation/Simulator.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>
using namespace threebody;
std::string temp() {
  char path[] = "/tmp/threebody-basic-XXXXXX";
  auto *p = mkdtemp(path);
  assert(p);
  return p;
}
std::string read(const std::string &p) {
  std::ifstream f(p);
  return {std::istreambuf_iterator<char>(f), {}};
}
struct Display : IRenderBackend {
  int delay;
  explicit Display(int ms) : delay(ms) {}
  bool initialize(const RenderConfig &) override { return true; }
  void shutdown() override {}
  BackendCapabilities queryCapabilities() const override { return {}; }
  FrameHandle submitFrame(const RenderFrameInput &) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    return {1};
  }
  bool isFrameComplete(FrameHandle) override { return true; }
  GpuTiming getGpuTiming(FrameHandle) override { return {}; }
  void resizePreview(uint32_t, uint32_t) override {}
};
std::string capture(int delay) {
  auto &sim = Simulator::instance();
  const auto directory = temp();
  sim.setRecordingDirectory(directory);
  RenderConfig config;
  config.enableGpuTiming = false;
  config.visualStyle = VisualStyle::Basic;
  assert(sim.start(makeFigureEightDouble(), std::make_shared<Display>(delay),
                   config));
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (sim.status().state.simulationTime < .3) {
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  sim.pause();
  auto t = sim.status().state.simulationTime;
  const auto file = sim.exportTrajectoryFile(false);
  const auto json = read(file);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  assert(sim.status().state.simulationTime == t);
  assert(read(sim.exportTrajectoryFile(false)) == json);
  assert(read(sim.exportTrajectoryFile(true)).find("star3_vz_km_s") !=
         std::string::npos);
  sim.stop();
  sim.setRecordingDirectory(directory);
  assert(sim.status().hasRecording);
  assert(read(sim.exportTrajectoryFile(false)) == json);
  std::ofstream("/tmp/threebody-basic-capture-" + std::to_string(delay) +
                ".json")
      << json;
  return json;
}
int main() {
  auto fast = capture(0), slow = capture(45);
  const auto key = "\"samples\":[";
  fast = fast.substr(fast.find(key));
  slow = slow.substr(slow.find(key));
  // Render stalls do not change a single bit of the first 20 physical
  // checkpoints.
  for (int i = 0; i < 20; ++i) {
    auto x = fast.find(']'), y = slow.find(']');
    assert(x != std::string::npos && y != std::string::npos);
    assert(fast.substr(0, x) == slow.substr(0, y));
    fast.erase(0, x + 1);
    slow.erase(0, y + 1);
  }
  const auto dir = temp();
  TrajectoryJournal journal;
  journal.configure(dir);
  auto state = makeFigureEightDouble();
  journal.begin("{\"test\":true}", state);
  for (int i = 1; i < 4200; ++i) {
    state.simulationTime = i * .01;
    journal.append(state);
    if (i % 500 == 0)
      journal.flush();
  }
  journal.finish();
  const auto exported = read(journal.exportFile(false));
  assert(exported.find("\"truncated\":false") != std::string::npos);
  size_t rows = 0;
  for (char c : exported)
    if (c == '[')
      ++rows;
  assert(rows == 4201);
  journal.configure(dir);
  assert(read(journal.exportFile(false)) == exported);
  TrajectoryJournal invalid;
  invalid.configure(dir + "/missing");
  invalid.begin("{}", state);
  invalid.flush();
  assert(!invalid.hasRecording());
  assert(!invalid.message().empty());
  std::cout << "PASS fixed checkpoints across render rates, atomic pause, "
               "JSON/CSV persistence, chunk rollover, write error\n";
}
