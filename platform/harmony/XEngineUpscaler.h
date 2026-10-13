#pragma once
#include "renderer/vulkan/VulkanUpscaler.h"
namespace threebody {
std::unique_ptr<VulkanUpscaler> makeXEngineUpscaler();
}
