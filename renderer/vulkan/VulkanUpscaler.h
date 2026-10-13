#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
namespace threebody {
// Optional platform-provided implementation; no vendor API enters the core.
class VulkanUpscaler {
public:
  virtual ~VulkanUpscaler() = default;
  virtual bool initialize(VkPhysicalDevice, VkDevice, VkExtent2D, VkExtent2D) = 0;
  virtual VkImage render(VkCommandBuffer, VkImageView) = 0;
  virtual VkImage outputImage() const { return VK_NULL_HANDLE; }
  virtual VkImageView outputView() const { return VK_NULL_HANDLE; }
  virtual std::string status() const = 0;
};
}
