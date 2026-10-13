#pragma once
#include "renderer/vulkan/VulkanFrameGenerator.h"
#include <memory>
namespace threebody {
std::unique_ptr<VulkanFrameGenerator> makeGraphicsFrameGenerator();
}
