#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace threebody {
void configureWindowFrameRate(uint64_t windowId);
void setWindowFrameRateActive(bool active);
std::string windowFrameRateStatus();
bool waitForWindowDisplayTick(const std::function<bool()>& cancelled);
}
