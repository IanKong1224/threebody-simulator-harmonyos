#pragma once
#include "renderer/common/IRenderBackend.h"
#include <memory>
#ifdef THREEBODY_RENDER_TESTING
#include <vector>
#endif
namespace threebody {
class GLESRenderBackend final : public IRenderBackend {
public:
  explicit GLESRenderBackend(void *nativeWindow);
  ~GLESRenderBackend() override;
  bool initialize(const RenderConfig &) override;
  void shutdown() override;
  BackendCapabilities queryCapabilities() const override;
  FrameHandle submitFrame(const RenderFrameInput &) override;
  bool isFrameComplete(FrameHandle) override;
  GpuTiming getGpuTiming(FrameHandle) override;
  void resizePreview(uint32_t, uint32_t) override;
  void presentFrame(FrameHandle) override;

#ifdef THREEBODY_RENDER_TESTING
  std::vector<uint8_t> readbackForTest();
#endif

private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};
} // namespace threebody
