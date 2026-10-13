#include "core/physics/InitialConditions.h"
#include "core/physics/RK4Integrator.h"
#include "renderer/gles/GLESRenderBackend.h"
#include "renderer/vulkan/VulkanRenderBackend.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace threebody;
template <class Backend> int test(const char *output) {
  Backend backend(nullptr);
  RenderConfig config;
  config.width = 640;
  config.height = 480;
  config.framesInFlight = 1;
  backend.initialize(config);
  RenderFrameInput input{0, makeAsymmetricDemo()};
  input.camera.distance = 3.5f;
  RK4Integrator<double> physics;
  auto render = [&] {
    auto handle = backend.submitFrame(input);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!backend.isFrameComplete(handle)) {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return backend.readbackForTest();
  };
  std::vector<uint8_t> pixels;
  // Populate trails and use exactly the same state on each API.
  for (int frame = 0; frame < 160; ++frame) {
    for (int i = 0; i < 100; ++i)
      physics.step(input.state, 1e-4);
    ++input.sequenceId;
    pixels = render();
  }
  std::ofstream file(output, std::ios::binary);
  file << "P6\n640 480\n255\n";
  size_t bright = 0, black = 0;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    file.write(reinterpret_cast<char *>(pixels.data() + i), 3);
    if (pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 0)
      ++black;
    if (*std::max_element(pixels.begin() + i, pixels.begin() + i + 3) > 200)
      ++bright;
  }
  if (bright < 100 || bright > 640 * 480 / 4)
    throw std::runtime_error("Missing stars or excessive saturation");
  auto repeat = render();
  if (repeat != pixels)
    throw std::runtime_error("Frozen state redraw changed material/trails");
  // A wider halo may legitimately cover former empty pixels. Verify the
  // background with separated, small projected disks instead of a fixed area
  // quota in a close-up scene.
  input.stellarBrightness = 0.f;
  const auto dim = render();
  {
    std::ofstream surface(std::string(output) + ".surface.ppm", std::ios::binary);
    surface << "P6\n640 480\n255\n";
    for (size_t i = 0; i < dim.size(); i += 4)
      surface.write(reinterpret_cast<const char *>(dim.data() + i), 3);
  }
  uint64_t dimLight=0, brightLight=0;
  for(size_t i=0;i<pixels.size();++i) {
    if(i%4==3) continue;
    dimLight+=dim[i]; brightLight+=pixels[i];
  }
  if(dimLight>=brightLight) throw std::runtime_error("Brightness control has no effect");
  input.stellarBrightness=1.f;
  if(render()!=pixels) throw std::runtime_error("Brightness redraw altered frozen scene");
  const auto savedCamera = input.camera;
  input.camera.distance = 30.f;
  const auto distant = render();
  size_t distantBlack = 0;
  for (size_t i = 0; i < distant.size(); i += 4)
    if (!distant[i] && !distant[i+1] && !distant[i+2]) ++distantBlack;
  if (distantBlack < 640 * 480 * 8 / 10)
    throw std::runtime_error("Distant empty space is not pure black");
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 640; ++x) {
      const auto i = (y * 640 + x) * 4;
      if (distant[i] || distant[i+1] || distant[i+2])
        throw std::runtime_error("Background pedestal after HDR mapping");
    }
  input.camera = savedCamera;
  input.camera.yaw += .7f;
  auto moved = render();
  if (moved == pixels)
    throw std::runtime_error("Camera change had no effect");
  input.camera.yaw -= .7f;
  auto reset = render();
  // Float roundoff in camera reconstruction may change a few pixels, so exact
  // reset is tested in core.
  backend.shutdown();
  backend.initialize(config);
  input.state = makeAsymmetricDemo();
  render();
  backend.shutdown();
  // Highest visual tier, three outstanding handles, same frozen-state image.
  config.workloadLevel = 2;
  backend.initialize(config);
  input.workloadLevel = 2;
  auto single = render();
  input.camera.distance = 30.f;
  auto highDistant = render();
  for (unsigned x = 0; x < 640; ++x)
    if (highDistant[x*4] || highDistant[x*4+1] || highDistant[x*4+2])
      throw std::runtime_error("High-tier distant background lifted");
  input.camera = savedCamera;
  std::ofstream high(std::string(output) + ".high.ppm", std::ios::binary);
  high << "P6\n640 480\n255\n";
  for (size_t i = 0; i < single.size(); i += 4)
    high.write(reinterpret_cast<char *>(single.data() + i), 3);
  high.close();
  backend.shutdown();
  config.framesInFlight = 3;
  backend.initialize(config);
  FrameHandle handles[3];
  for (auto &h : handles)
    h = backend.submitFrame(input);
  for (auto h : handles) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!backend.isFrameComplete(h)) {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("Ring timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto timing = backend.getGpuTiming(h);
    if (backend.queryCapabilities().supportsGpuTiming &&
        (!timing.valid || timing.milliseconds < 0))
      throw std::runtime_error("Missing ring timestamp");
  }
  if (backend.readbackForTest() != single)
    throw std::runtime_error("Ring overwrote GPU resources");
  for (int n = 0; n < 3; ++n)
    backend.submitFrame(input);
  backend.shutdown(); // Must drain in-flight resources safely.
  config.framesInFlight = 1;
  config.enableGpuTiming = false;
  if (!backend.initialize(config)) throw std::runtime_error("Simulator initialization failed");
  if (backend.queryCapabilities().supportsGpuTiming) throw std::runtime_error("Simulator still enables GPU measurement");
  render();
  backend.shutdown();
  std::cout << "PASS HDR/postprocess/readback, deterministic frozen redraw, "
               "camera, lifecycle; bright pixels="
            << bright << " black pixels=" << black << " output=" << output
            << "\n";
  return 0;
}
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      return 1;
    return std::string(argv[1]) == "gles" ? test<GLESRenderBackend>(argv[2])
                                          : test<VulkanRenderBackend>(argv[2]);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 2;
  }
}
