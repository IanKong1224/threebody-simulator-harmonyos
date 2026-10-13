#pragma once
#include "core/physics/ThreeBodyState.h"
#include <cstdint>
namespace threebody {
bool startHarmonySimulation(bool useGles, const ThreeBodyState<double> &initial,
                            uint32_t quality, double speed);
bool resumeHarmonySimulation();
bool setHarmonyAcceleration(bool enabled);
bool setHarmonyFrameGeneration(bool enabled);
void stopHarmonyPreview();
void restartHarmonyPreviewIfRunning();
} // namespace threebody
