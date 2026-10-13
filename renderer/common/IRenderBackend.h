#pragma once
#include "core/physics/ThreeBodyState.h"
#include "core/scene/Camera.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <string>

namespace threebody {

enum class VisualStyle : uint8_t { Detailed, Basic, Particles };

enum class GraphicsBackendType : uint8_t { Null, Vulkan, OpenGLES, Metal };

struct RenderConfig {
  uint32_t width{2560};
  uint32_t height{1440};
  uint32_t framesInFlight{3};
  uint32_t workloadLevel{0};
  bool enableGpuTiming{true};
  VisualStyle visualStyle{VisualStyle::Detailed};
  bool recordFrameRates{false};
  // Optional platform VSync gate. False means cancelled or unavailable; never catch up missed ticks.
  std::function<bool(const std::function<bool()>&)> waitForDisplayTick;
  uint32_t targetFps{60}; // Scheduling target, not a measured display refresh rate.
};

struct BackendCapabilities {
  GraphicsBackendType type{GraphicsBackendType::Null};
  bool supportsOffscreen{false};
  bool supportsGpuTiming{false};
  bool supportsPreviewSurface{false};
  bool supportsQueuedPresentation{false};
};

struct RenderFrameInput {
  uint64_t sequenceId{0};
  ThreeBodyState<double> state{};
  CameraState camera{};
  uint32_t workloadLevel{0};
  uint64_t sceneEpoch{0};
  float stellarBrightness{1.f};
  bool showTrails{true};
  bool allowFrameGeneration{false}; // Only advancing live preview; never physical/exported states.
  double trailDuration{12};
  std::shared_ptr<const std::vector<ThreeBodyState<double>>> history;
};

struct FrameHandle {
  uint64_t value{0};
};

struct GpuTiming {
  bool valid{false};
  double milliseconds{0.0};
  bool stagesValid{false};
  double stageMs[4]{}; // Scene, bright extraction, blur, compose/tone-map (including optional upscale).
};

struct DisplayTiming {
  bool supported{false};
  double fps{-1}; // Confirmed presentation timestamps only; -1 means unavailable/warming up.
};

class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;
  virtual bool initialize(const RenderConfig &config) = 0;
  virtual void shutdown() = 0;
  virtual BackendCapabilities queryCapabilities() const = 0;
  virtual FrameHandle submitFrame(const RenderFrameInput &input) = 0;
  virtual bool isFrameComplete(FrameHandle frame) = 0;
  virtual GpuTiming getGpuTiming(FrameHandle frame) = 0;
  // Optional preview is explicitly outside offscreen completion and timing.
  virtual void presentFrame(FrameHandle) {}
  virtual DisplayTiming pollDisplayTiming() { return {}; }
  // Optional N-0.5 presentation, before the corresponding real N.
  virtual bool presentIntermediateFrame(FrameHandle) { return false; }
  virtual bool frameGenerationActive() const { return false; }
  virtual std::string frameGenerationStatus() const { return "超帧关闭"; }
  // Completion of the copy submission, not confirmation of physical scanout.
  virtual bool isPresentationComplete(FrameHandle h) { return isFrameComplete(h); }
  virtual std::string accelerationStatus() const { return "原生渲染（未启用超分）"; }
  virtual void resizePreview(uint32_t width, uint32_t height) = 0;
};

} // namespace threebody
