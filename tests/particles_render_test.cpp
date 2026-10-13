#include "core/simulation/Simulator.h"
#include "renderer/common/BasicMesh.h"
#include "renderer/gles/GLESRenderBackend.h"
#include "renderer/vulkan/VulkanRenderBackend.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace threebody;
template <class Backend> void test(const char *output) {
  Backend backend(nullptr);
  RenderConfig config;
  config.width = 640;
  config.height = 480;
  config.framesInFlight = 1;
  config.visualStyle = VisualStyle::Particles;
  config.enableGpuTiming = false;
  backend.initialize(config);
  RenderFrameInput in;
  in.state = makeFigureEightDouble();
  in.camera.distance = 4;
  auto history = std::make_shared<std::vector<ThreeBodyState<double>>>();
  history->push_back(in.state);
  for (int i = 0; i < 450; ++i) {
    advanceSimulation(in.state, .01);
    history->push_back(in.state);
  }
  in.history = history;
  auto render = [&] {
    auto handle = backend.submitFrame(in);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!backend.isFrameComplete(handle)) {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("GPU timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return backend.readbackForTest();
  };
  const auto pixels = render();
  if (pixels != render())
    throw std::runtime_error("Frozen redraw changed the scene");
  size_t colored = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    if (pixels[i] || pixels[i + 1] || pixels[i + 2])
      ++colored;
  if (colored < 500 || colored > 640 * 480 / 3)
    throw std::runtime_error(
        "Missing lines/markers or unexpected full-screen effect");
  in.stellarBrightness = 0;
  if (render() != pixels)
    throw std::runtime_error("Particle edition still responds to HDR brightness");
  in.showTrails = false;
  const auto dots = render();
  size_t dotPixels = 0;
  for (size_t i = 0; i < dots.size(); i += 4)
    if (dots[i] || dots[i + 1] || dots[i + 2])
      ++dotPixels;
  if (dotPixels < 400 || dotPixels > 20000 || dotPixels >= colored)
    throw std::runtime_error("Trail toggle/point size");
  in.showTrails = true;
  in.trailDuration = 1;
  if (render() == pixels)
    throw std::runtime_error("Trail duration has no effect");
  in.camera.yaw += .5;
  if (render() == pixels)
    throw std::runtime_error("Camera did not move");
  std::ofstream file(output, std::ios::binary);
  file << "P6\n640 480\n255\n";
  for (size_t i = 0; i < pixels.size(); i += 4)
    file.write(reinterpret_cast<const char *>(pixels.data() + i), 3);
  backend.shutdown();
  backend.initialize(config);
  in.history = std::make_shared<const std::vector<ThreeBodyState<double>>>();
  render();
  backend.shutdown();
  std::cout << "PASS particles LDR spheres/disks, deterministic redraw, no "
               "brightness/bloom, trail toggle/length, camera/lifecycle: "
            << colored << " lit pixels\n";
}
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      return 1;
    if (std::string(argv[1]) == "gles")
      test<GLESRenderBackend>(argv[2]);
    else
      test<VulkanRenderBackend>(argv[2]);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
}
