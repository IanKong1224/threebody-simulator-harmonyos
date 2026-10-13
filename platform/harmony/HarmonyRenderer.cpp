#include "HarmonyRenderer.h"
#include "HarmonySurfaceState.h"
#include "HarmonyFrameRate.h"
#include "core/simulation/Simulator.h"
#include "renderer/gles/GLESRenderBackend.h"
#include "renderer/vulkan/VulkanRenderBackend.h"
#include <algorithm>
#ifdef THREEBODY_ACCELERATED
#include "XEngineUpscaler.h"
#include "GraphicsFrameGenerator.h"
#endif
namespace threebody {
namespace {
bool gles = false;
bool accelerated = false;
bool frameGeneration = false;
uint32_t quality = 0;
double speed = 1;
bool launch(const ThreeBodyState<double> &state, bool run,
            bool continuation = false) {
  auto surface = HarmonySurfaceState::instance().snapshot();
  if (!surface.attached || !surface.width || !surface.height)
    return false;
  std::shared_ptr<IRenderBackend> renderer;
  if (gles)
    renderer = std::make_shared<GLESRenderBackend>(surface.nativeWindow);
  else {
    std::unique_ptr<VulkanUpscaler> upscale;
    std::unique_ptr<VulkanFrameGenerator> generator;
#ifdef THREEBODY_ACCELERATED
    if (accelerated) upscale = makeXEngineUpscaler();
    if (frameGeneration) generator = makeGraphicsFrameGenerator();
#endif
    renderer = std::make_shared<VulkanRenderBackend>(surface.nativeWindow, std::move(upscale), std::move(generator));
  }
  RenderConfig config;
#if defined(THREEBODY_ACCELERATED) || defined(THREEBODY_PARTICLES)
  config.waitForDisplayTick = waitForWindowDisplayTick;
#endif
  config.width = surface.width;
  config.height = surface.height;
  config.framesInFlight = 1;
#ifndef THREEBODY_BASIC
  config.targetFps = 120;
#endif
#if defined(THREEBODY_BASIC) || defined(THREEBODY_PARTICLES) || defined(THREEBODY_ACCELERATED)
  config.recordFrameRates = true;
#endif
  config.workloadLevel = quality;
  config.enableGpuTiming = false;
#ifdef THREEBODY_ACCELERATED
  config.enableGpuTiming = true;
#endif
// Queueing is a renderer optimization, independent of XEngine/upscaling.
// Keep two slots in all sphere-based editions so CPU preparation overlaps GPU work.
#if defined(THREEBODY_PARTICLES) || defined(THREEBODY_DETAILED) || defined(THREEBODY_ACCELERATED)
  if (!gles) config.framesInFlight = 2;
#endif
#ifdef THREEBODY_PARTICLES
  config.visualStyle = VisualStyle::Particles;
#elif defined(THREEBODY_BASIC)
  config.visualStyle = VisualStyle::Basic;
#endif
  return Simulator::instance().start(state, renderer, config, speed, run,
                                     continuation);
}
} // namespace
bool startHarmonySimulation(bool useGles, const ThreeBodyState<double> &initial,
                            uint32_t selectedQuality, double selectedSpeed) {
  if (!validateInitialState(initial).empty())
    return false;
  gles = useGles;
  quality = std::min(2u, selectedQuality);
  speed = selectedSpeed;
  return launch(initial, true);
}
bool resumeHarmonySimulation() {
  auto &sim = Simulator::instance();
  if (sim.resume())
    return true;
  auto s = sim.status();
  return s.presentedFrames > 0 && launch(s.state, true, true);
}
bool setHarmonyAcceleration(bool enabled) {
  if (accelerated == enabled) return true;
  accelerated = enabled;
  auto& sim = Simulator::instance();
  auto before = sim.status();
  if (!before.available) return true;
  sim.pause(); // Freeze at a complete physics checkpoint before taking a snapshot.
  auto frozen = sim.status();
  return launch(frozen.state, before.running, true);
}
bool setHarmonyFrameGeneration(bool enabled) {
  if(frameGeneration==enabled)return true;
  frameGeneration=enabled;
  auto& sim=Simulator::instance();auto before=sim.status();
  if(!before.available)return true;
  sim.pause();return launch(sim.status().state,before.running,true);
}
void stopHarmonyPreview() { Simulator::instance().stop(); }
void restartHarmonyPreviewIfRunning() {
  auto s = Simulator::instance().status();
  if (s.presentedFrames > 0)
    launch(s.state, s.running, true);
}
} // namespace threebody
