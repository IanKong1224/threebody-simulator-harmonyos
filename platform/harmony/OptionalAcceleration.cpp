#include "XEngineUpscaler.h"
#include "GraphicsFrameGenerator.h"
namespace threebody {
#ifndef THREEBODY_HAS_XENGINE
class UnavailableUpscaler final : public VulkanUpscaler {
public:
  bool initialize(VkPhysicalDevice, VkDevice, VkExtent2D, VkExtent2D) override { return false; }
  VkImage render(VkCommandBuffer, VkImageView) override { return VK_NULL_HANDLE; }
  std::string status() const override { return "此构建未包含 XEngine SDK；使用原生渲染"; }
};
std::unique_ptr<VulkanUpscaler> makeXEngineUpscaler() {
  return std::unique_ptr<VulkanUpscaler>(new UnavailableUpscaler);
}
#endif
#ifndef THREEBODY_HAS_FRAMEGEN
class UnavailableFrameGenerator final : public VulkanFrameGenerator {
public:
  bool initialize(const VulkanFrameGenerationConfig&) override { return false; }
  VkImage dispatch(VkCommandBuffer, uint32_t, const Matrix4&) override { return VK_NULL_HANDLE; }
  void resetHistory() override {}
  bool active() const override { return false; }
  std::string status() const override { return "此构建未包含 Graphics 超帧 SDK；使用真实渲染帧"; }
};
std::unique_ptr<VulkanFrameGenerator> makeGraphicsFrameGenerator() {
  return std::unique_ptr<VulkanFrameGenerator>(new UnavailableFrameGenerator);
}
#endif
}
