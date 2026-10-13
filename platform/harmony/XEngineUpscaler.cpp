#include "XEngineUpscaler.h"
#define XEG_NO_PROTOTYPES
#include <xengine/xeg_vulkan_extension.h>
#include <xengine/xeg_vulkan_spatial_upscale.h>
#include <syscap_ndk.h>
#include <dlfcn.h>
#include <cstring>
#include <vector>
namespace threebody {
namespace {
class XEngineUpscaler final : public VulkanUpscaler {
  void* library_{};
  VkDevice device_{};
  XEG_SpatialUpscale upscale_{};
  VkImage image_{};
  VkDeviceMemory memory_{};
  VkImageView view_{};
  PFN_HMS_XEG_CreateSpatialUpscale create_{};
  PFN_HMS_XEG_DestroySpatialUpscale destroy_{};
  PFN_HMS_XEG_CmdRenderSpatialUpscale render_{};
  std::string status_{"XEngine 尚未初始化"};
public:
  ~XEngineUpscaler() override {
    // Owner has waited for GPU completion before releasing this object.
    if (upscale_ && destroy_) destroy_(upscale_);
    if (view_) vkDestroyImageView(device_, view_, nullptr);
    if (image_) vkDestroyImage(device_, image_, nullptr);
    if (memory_) vkFreeMemory(device_, memory_, nullptr);
    if (library_) dlclose(library_);
  }
  VkImage outputImage() const override { return image_; }
  VkImageView outputView() const override { return view_; }
  std::string status() const override { return status_; }
  bool initialize(VkPhysicalDevice physical, VkDevice device, VkExtent2D input, VkExtent2D output) override {
    device_ = device;
    status_ = "设备不支持 XEngine；已回退原生渲染";
    if (!canIUse("SystemCapability.Graphic.XEngine")) return false;
    library_ = dlopen("libxengine.so", RTLD_NOW | RTLD_LOCAL);
    if (!library_) return false;
    auto enumerate = reinterpret_cast<PFN_HMS_XEG_EnumerateDeviceExtensionProperties>(dlsym(library_, "HMS_XEG_EnumerateDeviceExtensionProperties"));
    create_ = reinterpret_cast<PFN_HMS_XEG_CreateSpatialUpscale>(dlsym(library_, "HMS_XEG_CreateSpatialUpscale"));
    destroy_ = reinterpret_cast<PFN_HMS_XEG_DestroySpatialUpscale>(dlsym(library_, "HMS_XEG_DestroySpatialUpscale"));
    render_ = reinterpret_cast<PFN_HMS_XEG_CmdRenderSpatialUpscale>(dlsym(library_, "HMS_XEG_CmdRenderSpatialUpscale"));
    if (!enumerate || !create_ || !destroy_ || !render_) return false;
    uint32_t count = 0;
    if (enumerate(physical, &count, nullptr) != VK_SUCCESS || !count || count > 4096) return false;
    std::vector<XEG_ExtensionProperties> extensions(count);
    if (enumerate(physical, &count, extensions.data()) != VK_SUCCESS) return false;
    bool supported = false;
    for (const auto& e : extensions)
      supported |= std::strcmp(e.extensionName, XEG_SPATIAL_UPSCALE_EXTENSION_NAME) == 0;
    if (!supported) return false;
    status_ = "XEngine 初始化失败；已回退原生渲染";
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &props);
    const auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT;
    if ((props.optimalTilingFeatures & required) != required) return false;
    XEG_SpatialUpscaleCreateInfo info{};
    info.inputSize = input; info.inputRegion = {{0,0}, input};
    info.outputSize = output; info.outputRegion = {{0,0}, output};
    info.format = VK_FORMAT_R8G8B8A8_UNORM; info.sharpness = .2f;
    if (create_(device, &info, &upscale_) != VK_SUCCESS || !upscale_) return false;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D; image.format = info.format;
    image.extent = {output.width, output.height, 1}; image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateImage(device, &image, nullptr, &image_) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, image_, &req);
    VkPhysicalDeviceMemoryProperties mp{}; vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    uint32_t index = 0;
    while (index < mp.memoryTypeCount && (!(req.memoryTypeBits & (1u << index)) ||
           !(mp.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) ++index;
    if (index == mp.memoryTypeCount) return false;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size; alloc.memoryTypeIndex = index;
    if (vkAllocateMemory(device, &alloc, nullptr, &memory_) != VK_SUCCESS ||
        vkBindImageMemory(device, image_, memory_, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image_; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if (vkCreateImageView(device, &view, nullptr, &view_) != VK_SUCCESS) return false;
    status_ = "XEngine 已启用 · " + std::to_string(input.width) + "×" + std::to_string(input.height) +
              " → " + std::to_string(output.width) + "×" + std::to_string(output.height);
    return true;
  }
  VkImage render(VkCommandBuffer cmd, VkImageView input) override {
    // Huawei Vulkan codelab: sampled input; XEngine owns the output render pass,
    // whose final layout is SHADER_READ_ONLY_OPTIMAL. Restore it after copying.
    XEG_SpatialUpscaleDescription desc{input, view_};
    render_(cmd, upscale_, &desc);
    return image_;
  }
};
}
std::unique_ptr<VulkanUpscaler> makeXEngineUpscaler() {
  return std::unique_ptr<VulkanUpscaler>(new XEngineUpscaler);
}
}
