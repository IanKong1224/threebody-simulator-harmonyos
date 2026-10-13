#pragma once
#include <chrono>
#include <cstdint>

namespace threebody {
// The caller validates targetFps (60 or 120). This is a minimum interval;
// scheduling or rendering may take longer. Generated pairs occupy two slots.
inline std::chrono::nanoseconds framePacingInterval(unsigned targetFps,
                                                    bool generatedPair = false) {
  return std::chrono::nanoseconds((generatedPair ? 2000000000LL : 1000000000LL) /
                                  targetFps);
}
} // namespace threebody
