#pragma once
#include <cstdint>
#include <mutex>

namespace threebody {

struct HarmonySurfaceSnapshot {
    bool attached{false};
    void* nativeWindow{nullptr};
    uint64_t width{0};
    uint64_t height{0};
};

class HarmonySurfaceState {
public:
    static HarmonySurfaceState& instance();
    void onCreated(void* nativeWindow, uint64_t width, uint64_t height);
    void onChanged(void* nativeWindow, uint64_t width, uint64_t height);
    void onDestroyed(void* nativeWindow);
    HarmonySurfaceSnapshot snapshot() const;
private:
    mutable std::mutex mutex_;
    HarmonySurfaceSnapshot state_{};
};

} // namespace threebody
