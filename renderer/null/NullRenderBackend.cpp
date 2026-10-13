#include "renderer/null/NullRenderBackend.h"

namespace threebody {

BackendCapabilities NullRenderBackend::queryCapabilities() const {
    BackendCapabilities c{};
    c.type = GraphicsBackendType::Null;
    c.supportsOffscreen = true;
    return c;
}

FrameHandle NullRenderBackend::submitFrame(const RenderFrameInput&) {
    return FrameHandle{nextFrame_++};
}

} // namespace threebody
