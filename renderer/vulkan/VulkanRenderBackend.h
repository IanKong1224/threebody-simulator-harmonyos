#pragma once
#include "renderer/common/IRenderBackend.h"
#include <memory>
#include "VulkanUpscaler.h"
#include "VulkanFrameGenerator.h"
#ifdef THREEBODY_RENDER_TESTING
#include <vector>
#endif
namespace threebody {
class VulkanRenderBackend final : public IRenderBackend {
public:
  explicit VulkanRenderBackend(void *nativeWindow, std::unique_ptr<VulkanUpscaler> upscaler = nullptr,
                               std::unique_ptr<VulkanFrameGenerator> generator = nullptr);
  std::string accelerationStatus() const override;
  ~VulkanRenderBackend() override;
  bool initialize(const RenderConfig &) override;
  void shutdown() override;
  BackendCapabilities queryCapabilities() const override;
  FrameHandle submitFrame(const RenderFrameInput &) override;
  bool isFrameComplete(FrameHandle) override;
  GpuTiming getGpuTiming(FrameHandle) override;
  void resizePreview(uint32_t, uint32_t) override;
  void presentFrame(FrameHandle) override;
  bool isPresentationComplete(FrameHandle) override;
  DisplayTiming pollDisplayTiming() override;
  bool presentIntermediateFrame(FrameHandle) override;
  bool frameGenerationActive() const override;
  std::string frameGenerationStatus() const override;

#ifdef THREEBODY_RENDER_TESTING
  std::vector<uint8_t> readbackForTest(bool generated = false);
#endif

private:
  void presentImage(FrameHandle, bool intermediate);
  struct Impl;
  std::unique_ptr<Impl> p_;
};
} // namespace threebody
