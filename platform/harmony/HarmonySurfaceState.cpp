#include "platform/harmony/HarmonySurfaceState.h"

namespace threebody {
HarmonySurfaceState& HarmonySurfaceState::instance() {
    static HarmonySurfaceState s;
    return s;
}
void HarmonySurfaceState::onCreated(void* w, uint64_t width, uint64_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = {true, w, width, height};
}
void HarmonySurfaceState::onChanged(void* w, uint64_t width, uint64_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = {true, w, width, height};
}
void HarmonySurfaceState::onDestroyed(void*) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = {};
}
HarmonySurfaceSnapshot HarmonySurfaceState::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
} // namespace threebody
