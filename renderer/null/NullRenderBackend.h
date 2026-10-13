#pragma once
#include "renderer/common/IRenderBackend.h"

namespace threebody {

class NullRenderBackend final : public IRenderBackend {
public:
    bool initialize(const RenderConfig&) override { initialized_ = true; return true; }
    void shutdown() override { initialized_ = false; }
    BackendCapabilities queryCapabilities() const override;
    FrameHandle submitFrame(const RenderFrameInput&) override;
    bool isFrameComplete(FrameHandle) override { return initialized_; }
    GpuTiming getGpuTiming(FrameHandle) override { return {}; }
    void resizePreview(uint32_t, uint32_t) override {}
private:
    bool initialized_{false};
    uint64_t nextFrame_{1};
};

} // namespace threebody
