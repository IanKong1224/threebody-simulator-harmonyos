#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include "core/scene/Camera.h"
namespace threebody {
struct VulkanFrameGenerationConfig {
  VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{};
  uint32_t slots{};
  VkExtent2D colorExtent{}, depthExtent{};
  VkFormat colorFormat{}, depthFormat{};
  VkImage color{}, depth{}; VkImageView colorView{}, depthView{};
};
// Vendor implementation belongs to platform/harmony. Dispatch restores input
// layouts and returns an optional TRANSFER_SRC image for N-0.5, before real N.
class VulkanFrameGenerator {
public:
  virtual ~VulkanFrameGenerator() = default;
  virtual bool initialize(const VulkanFrameGenerationConfig&) = 0;
  virtual VkImage dispatch(VkCommandBuffer, uint32_t slot, const Matrix4&) = 0;
  virtual void resetHistory() = 0;
  virtual bool active() const = 0;
  virtual std::string status() const = 0;
};
}
