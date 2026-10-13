#define VK_USE_PLATFORM_OHOS
#include "renderer/vulkan/VulkanRenderBackend.h"
#include "renderer/common/BasicMesh.h"
#include "renderer/common/SceneMesh.h"
#include "renderer/vulkan/SceneShaders.h"
#include <cstddef>
#include <chrono>
#include <algorithm>
#include <deque>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
namespace threebody {
namespace {
void check(VkResult r, const char *op) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(op) + ": " + std::to_string(r));
}
} // namespace
struct VulkanRenderBackend::Impl {
  void *window;
  std::unique_ptr<VulkanUpscaler> upscaler;
  std::unique_ptr<VulkanFrameGenerator> generator;
  std::string generationMessage{"超帧关闭"};
  bool generationWasRunning{false};
  VkImage generatedImage{};
  VkExtent2D displayExtent{};
  VkImage upscaledImage{};
  std::string acceleration{"原生渲染（加速关闭）"};
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  uint32_t family{};
  VkSurfaceKHR surface{};
  VkSwapchainKHR swap{};
  PFN_vkGetPastPresentationTimingGOOGLE pastTiming{};
  uint32_t presentId{0};
  std::deque<uint64_t> displayTimes;
  std::chrono::steady_clock::time_point timingArrival{};

  VkFormat format{};
  VkExtent2D extent{};
  std::vector<VkImage> images;
  VkImage offscreen{};
  VkDeviceMemory memory{};
  VkImageView view{}, depthView{};
  VkImage depthImage{};
  VkDeviceMemory depthMemory{};
  VkFormat depthFormat{};
  struct Slot {
    VkBuffer vertices{};
    VkDeviceMemory vertexMemory{};
    void *mapped{};
    VkCommandBuffer draw{};
    VkFence drawFence{};
    VkCommandBuffer presentCopy{};
    VkFence presentFence{};
    VkSemaphore acquired{};
    VkCommandBuffer intermediateCopy{};
    VkSemaphore intermediateAcquired{};
    bool generated{false}, intermediatePresented{false};
    bool presentSubmitted{false};
    uint64_t serial{};
  };
  std::vector<Slot> slots;

  VkShaderModule vertexShader{}, fragmentShader{};
  VkPipelineLayout layout{};
  VkPipeline pipeline{};
  VkQueryPool timestamps{};
  uint32_t timestampBits{};
  double timestampPeriod{};
  struct ColorTarget {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
  } hdr, bloom[2];
  VkExtent2D bloomExtent{};
  uint32_t workloadLevel{0};
  VkRenderPass bloomPass{}, finalPass{};
  VkFramebuffer bloomFramebuffer[2]{}, finalFramebuffer{};
  VkPipeline bloomPipeline{}, finalPipeline{};
  VkPipelineLayout postLayout{};
  VkShaderModule postVertex{}, postFragment{};
  VkDescriptorSetLayout descriptorLayout{};
  VkDescriptorPool descriptorPool{};
  VkDescriptorSet descriptorSets[5]{};
  VkSampler sampler{};
  VkRenderPass pass{};
  VkFramebuffer framebuffer{};
  VkCommandPool pool{};
  VkCommandBuffer copy{};
  VkFence copyFence{};
  VkSemaphore acquired{};
  std::vector<VkSemaphore> presented;
  uint64_t serial{}, completed{}, lastPresented{}, presentationCompleted{};

  SceneMesh geometry;
  BasicMesh basicGeometry;
  bool basic{false};
  bool particles{false};
  explicit Impl(void *w) : window(w) {}
  void barrier(VkCommandBuffer c, VkImage image, VkImageLayout oldLayout,
               VkImageLayout newLayout, VkAccessFlags src, VkAccessFlags dst,
               VkPipelineStageFlags a, VkPipelineStageFlags b) {
    VkImageMemoryBarrier v{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    v.srcAccessMask = src;
    v.dstAccessMask = dst;
    v.oldLayout = oldLayout;
    v.newLayout = newLayout;
    v.srcQueueFamilyIndex = v.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    v.image = image;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(c, a, b, 0, 0, nullptr, 0, nullptr, 1, &v);
  }
};
VulkanRenderBackend::VulkanRenderBackend(void *w, std::unique_ptr<VulkanUpscaler> upscaler, std::unique_ptr<VulkanFrameGenerator> generator) : p_(new Impl(w)) { p_->upscaler = std::move(upscaler); p_->generator = std::move(generator); }
std::string VulkanRenderBackend::accelerationStatus() const { return p_->acceleration; }
bool VulkanRenderBackend::frameGenerationActive() const { return p_->generator && p_->generator->active(); }
std::string VulkanRenderBackend::frameGenerationStatus() const { return p_->generator ? p_->generator->status() : p_->generationMessage; }
VulkanRenderBackend::~VulkanRenderBackend() { shutdown(); }
bool VulkanRenderBackend::initialize(const RenderConfig &config) {
  auto &p = *p_;
  if (config.framesInFlight < 1 || config.framesInFlight > 3 ||
      (p.window && config.framesInFlight > 2))
    throw std::runtime_error(
        "Supported in-flight capacity: headless 1..3, preview 1..2");
  p.slots.resize(config.framesInFlight);
  p.particles = config.visualStyle == VisualStyle::Particles;
  p.basic = config.visualStyle != VisualStyle::Detailed;
  p.workloadLevel = std::min(config.workloadLevel, uint32_t(2));
  try {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ThreeBodyPreview";
    app.apiVersion = VK_API_VERSION_1_0;
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                VK_OHOS_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = p.window ? 2 : 0;
    ici.ppEnabledExtensionNames = extensions;
    check(vkCreateInstance(&ici, nullptr, &p.instance), "create instance");
    if (p.window) {
      VkSurfaceCreateInfoOHOS si{VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS};
      si.window = static_cast<decltype(si.window)>(p.window);
      auto create = reinterpret_cast<PFN_vkCreateSurfaceOHOS>(
          vkGetInstanceProcAddr(p.instance, "vkCreateSurfaceOHOS"));
      if (!create)
        throw std::runtime_error("OHOS surface extension unavailable");
      check(create(p.instance, &si, nullptr, &p.surface), "create surface");
    }
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(p.instance, &count, nullptr),
          "enumerate devices");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(p.instance, &count, devices.data()),
          "enumerate devices");
    for (auto d : devices) {
      uint32_t n = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
      std::vector<VkQueueFamilyProperties> q(n);
      vkGetPhysicalDeviceQueueFamilyProperties(d, &n, q.data());
      for (uint32_t i = 0; i < n; ++i) {
        VkBool32 present = VK_TRUE;
        if (p.surface)
          check(vkGetPhysicalDeviceSurfaceSupportKHR(d, i, p.surface, &present),
                "surface support");
        if ((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
          p.physical = d;
          p.family = i;
          p.timestampBits = q[i].timestampValidBits;
          break;
        }
      }
      if (p.physical)
        break;
    }
    if (!p.physical)
      throw std::runtime_error("No graphics/present queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = p.family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    std::vector<const char*> deviceExtensions;
    if (p.surface) {
      deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
      uint32_t count=0;
      check(vkEnumerateDeviceExtensionProperties(p.physical,nullptr,&count,nullptr),"device extension count");
      std::vector<VkExtensionProperties> extensions(count);
      check(vkEnumerateDeviceExtensionProperties(p.physical,nullptr,&count,extensions.data()),"device extensions");
      for(const auto& e:extensions) if(std::strcmp(e.extensionName,VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME)==0)
        deviceExtensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    }
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    di.ppEnabledExtensionNames = deviceExtensions.data();
    check(vkCreateDevice(p.physical, &di, nullptr, &p.device), "create device");
    vkGetDeviceQueue(p.device, p.family, 0, &p.queue);
    if(deviceExtensions.size()>1) p.pastTiming=reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(vkGetDeviceProcAddr(p.device,"vkGetPastPresentationTimingGOOGLE"));
    p.format = VK_FORMAT_R8G8B8A8_UNORM;
    p.extent = {config.width, config.height};
    if (p.surface) {
      VkSurfaceCapabilitiesKHR caps{};
      check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(p.physical, p.surface,
                                                      &caps),
            "surface capabilities");
      uint32_t n = 0;
      check(vkGetPhysicalDeviceSurfaceFormatsKHR(p.physical, p.surface, &n,
                                                 nullptr),
            "surface formats");
      std::vector<VkSurfaceFormatKHR> fs(n);
      check(vkGetPhysicalDeviceSurfaceFormatsKHR(p.physical, p.surface, &n,
                                                 fs.data()),
            "surface formats");
      if (fs.empty())
        throw std::runtime_error("No surface formats");
      auto f = fs[0];
      if (f.format == VK_FORMAT_UNDEFINED)
        f.format = p.format;
      for (auto candidate : fs)
        if (candidate.format == VK_FORMAT_R8G8B8A8_UNORM ||
            candidate.format == VK_FORMAT_B8G8R8A8_UNORM) {
          f = candidate;
          break;
        }
      p.format = f.format;
      if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        p.extent = caps.currentExtent;
      else {
        p.extent.width =
            std::max(caps.minImageExtent.width,
                     std::min(config.width, caps.maxImageExtent.width));
        p.extent.height =
            std::max(caps.minImageExtent.height,
                     std::min(config.height, caps.maxImageExtent.height));
      }
      if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        throw std::runtime_error("Swapchain transfer destination unsupported");
      uint32_t imageCount = caps.minImageCount + 1;
      if (caps.maxImageCount)
        imageCount = std::min(imageCount, caps.maxImageCount);
      VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
      sc.surface = p.surface;
      sc.minImageCount = imageCount;
      sc.imageFormat = f.format;
      sc.imageColorSpace = f.colorSpace;
      sc.imageExtent = p.extent;
      sc.imageArrayLayers = 1;
      sc.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      sc.preTransform = caps.currentTransform;
      for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                         VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                         VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                         VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
        if (caps.supportedCompositeAlpha & alpha) {
          sc.compositeAlpha = alpha;
          break;
        }
      sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
      sc.clipped = VK_TRUE;
      check(vkCreateSwapchainKHR(p.device, &sc, nullptr, &p.swap),
            "create swapchain");
      check(vkGetSwapchainImagesKHR(p.device, p.swap, &n, nullptr),
            "swapchain images");
      p.images.resize(n);
      check(vkGetSwapchainImagesKHR(p.device, p.swap, &n, p.images.data()),
            "swapchain images");
    }
    p.displayExtent = p.extent;
    if (p.upscaler) {
      VkFormatProperties outputProps{};
      vkGetPhysicalDeviceFormatProperties(p.physical, p.format, &outputProps);
      VkExtent2D reduced{std::max(1u, p.extent.width * 3 / 4), std::max(1u, p.extent.height * 3 / 4)};
      if (config.framesInFlight <= 2 &&
          (p.format == VK_FORMAT_R8G8B8A8_UNORM || p.format == VK_FORMAT_B8G8R8A8_UNORM) &&
          (outputProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) &&
          p.upscaler->initialize(p.physical, p.device, reduced, p.displayExtent)) {
        p.extent = reduced;
        p.format = VK_FORMAT_R8G8B8A8_UNORM;
        p.acceleration = p.upscaler->status();
      } else {
        p.acceleration = p.upscaler->status();
        if (p.acceleration == "XEngine 尚未初始化")
          p.acceleration = "当前输出格式不支持加速；已回退原生渲染";
        p.upscaler.reset();
      }
    }
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = p.format;
    ii.extent = {p.extent.width, p.extent.height, 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(p.device, &ii, nullptr, &p.offscreen),
          "offscreen image");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(p.device, p.offscreen, &req);
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(p.physical, &mp);
    uint32_t type = 0;
    while (type < mp.memoryTypeCount &&
           (!(req.memoryTypeBits & (1u << type)) ||
            !(mp.memoryTypes[type].propertyFlags &
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
      ++type;
    if (type == mp.memoryTypeCount)
      throw std::runtime_error("No image memory type");
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = req.size;
    ma.memoryTypeIndex = type;
    check(vkAllocateMemory(p.device, &ma, nullptr, &p.memory), "image memory");
    check(vkBindImageMemory(p.device, p.offscreen, p.memory, 0),
          "bind image memory");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = p.offscreen;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = p.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(p.device, &vi, nullptr, &p.view), "image view");
    if (!p.basic) {
      VkFormatProperties hdrProperties{};
      vkGetPhysicalDeviceFormatProperties(
          p.physical, VK_FORMAT_R16G16B16A16_SFLOAT, &hdrProperties);
      const VkFormatFeatureFlags hdrRequired =
          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      if ((hdrProperties.optimalTilingFeatures & hdrRequired) != hdrRequired)
        throw std::runtime_error(
            "M6 requires renderable, linearly sampled RGBA16F");
      auto colorTarget = [&](Impl::ColorTarget &target, VkExtent2D extent) {
        auto info = ii;
        info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.extent = {extent.width, extent.height, 1};
        info.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        check(vkCreateImage(p.device, &info, nullptr, &target.image),
              "HDR image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(p.device, target.image, &requirements);
        uint32_t index = 0;
        while (index < mp.memoryTypeCount &&
               (!(requirements.memoryTypeBits & (1u << index)) ||
                !(mp.memoryTypes[index].propertyFlags &
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
          ++index;
        if (index == mp.memoryTypeCount)
          throw std::runtime_error("No HDR image memory");
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = index;
        check(vkAllocateMemory(p.device, &alloc, nullptr, &target.memory),
              "HDR memory");
        check(vkBindImageMemory(p.device, target.image, target.memory, 0),
              "HDR bind");
        auto view = vi;
        view.image = target.image;
        view.format = info.format;
        check(vkCreateImageView(p.device, &view, nullptr, &target.view),
              "HDR view");
      };
      p.bloomExtent = {std::max(1u, p.extent.width / 4u),
                       std::max(1u, p.extent.height / 4u)};
      colorTarget(p.hdr, p.extent);
      for (auto &target : p.bloom)
        colorTarget(target, p.bloomExtent);
    }
    // Dedicated offscreen depth buffer, independent of the swapchain.
    auto selectDepth = [&](bool sampled) {
      for(auto format : {VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM}) {
        VkFormatProperties props{};vkGetPhysicalDeviceFormatProperties(p.physical,format,&props);
        VkFormatFeatureFlags required=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if(sampled)required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if((props.optimalTilingFeatures & required)==required){p.depthFormat=format;return;}
      }
    };
    selectDepth(bool(p.generator));
    if(!p.depthFormat && p.generator){
      p.generationMessage="深度格式不支持超帧；真实渲染目标 120";
      p.generator.reset();selectDepth(false);
    }
    if (!p.depthFormat)
      throw std::runtime_error("No supported depth format");
    auto depthInfo = ii;
    depthInfo.format = p.depthFormat;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(p.generator) depthInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    check(vkCreateImage(p.device, &depthInfo, nullptr, &p.depthImage),
          "depth image");
    vkGetImageMemoryRequirements(p.device, p.depthImage, &req);
    uint32_t depthType = 0;
    while (depthType < mp.memoryTypeCount &&
           (!(req.memoryTypeBits & (1u << depthType)) ||
            !(mp.memoryTypes[depthType].propertyFlags &
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
      ++depthType;
    if (depthType == mp.memoryTypeCount)
      throw std::runtime_error("No depth memory type");
    ma.allocationSize = req.size;
    ma.memoryTypeIndex = depthType;
    check(vkAllocateMemory(p.device, &ma, nullptr, &p.depthMemory),
          "depth memory");
    check(vkBindImageMemory(p.device, p.depthImage, p.depthMemory, 0),
          "bind depth");
    auto depthVI = vi;
    depthVI.image = p.depthImage;
    depthVI.format = p.depthFormat;
    depthVI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    check(vkCreateImageView(p.device, &depthVI, nullptr, &p.depthView),
          "depth view");
    VkAttachmentDescription ad{};
    ad.format = p.basic ? p.format : VK_FORMAT_R16G16B16A16_SFLOAT;
    ad.samples = VK_SAMPLE_COUNT_1_BIT;
    ad.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ad.finalLayout = p.basic ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                             : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentDescription attachments[2] = {ad, {}};
    attachments[1].format = p.depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = p.generator ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ar;
    sp.pDepthStencilAttachment = &depthReference;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = p.basic ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                   : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask =
        p.basic ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 2;
    rp.pAttachments = attachments;
    rp.subpassCount = 1;
    rp.pSubpasses = &sp;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    check(vkCreateRenderPass(p.device, &rp, nullptr, &p.pass), "render pass");
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = p.pass;
    VkImageView views[] = {p.basic ? p.view : p.hdr.view, p.depthView};
    fb.attachmentCount = 2;
    fb.pAttachments = views;
    fb.width = p.extent.width;
    fb.height = p.extent.height;
    fb.layers = 1;
    check(vkCreateFramebuffer(p.device, &fb, nullptr, &p.framebuffer),
          "framebuffer");
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.queueFamilyIndex = p.family;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(p.device, &cp, nullptr, &p.pool), "command pool");
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = p.pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    for (auto &slot : p.slots)
      check(vkAllocateCommandBuffers(p.device, &ca, &slot.draw),
            "draw command");
    check(vkAllocateCommandBuffers(p.device, &ca, &p.copy), "copy command");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for (auto &slot : p.slots)
      check(vkCreateFence(p.device, &fi, nullptr, &slot.drawFence),
            "draw fence");
    check(vkCreateFence(p.device, &fi, nullptr, &p.copyFence), "copy fence");
    VkSemaphoreCreateInfo se{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(p.device, &se, nullptr, &p.acquired),
          "acquire semaphore");
    if (p.surface) for (auto &slot : p.slots) {
      check(vkAllocateCommandBuffers(p.device, &ca, &slot.presentCopy), "slot copy command");
      check(vkCreateFence(p.device, &fi, nullptr, &slot.presentFence), "slot copy fence");
      check(vkCreateSemaphore(p.device, &se, nullptr, &slot.acquired), "slot acquire semaphore");
      if(p.generator){
        check(vkAllocateCommandBuffers(p.device,&ca,&slot.intermediateCopy),"intermediate copy command");
        check(vkCreateSemaphore(p.device,&se,nullptr,&slot.intermediateAcquired),"intermediate acquire semaphore");
      }
    }
    p.presented.resize(p.images.size());
    for (auto &sem : p.presented)
      check(vkCreateSemaphore(p.device, &se, nullptr, &sem),
            "present semaphore");
    for (auto &slot : p.slots) {
      VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      buffer.size =
          ((p.basic && !p.particles) ? BasicMesh::maxVertices : SceneMesh::maxVertices) *
          sizeof(SceneVertex);
      buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
      buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      check(vkCreateBuffer(p.device, &buffer, nullptr, &slot.vertices),
            "vertex buffer");
      vkGetBufferMemoryRequirements(p.device, slot.vertices, &req);
      uint32_t hostType = 0;
      const auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      while (hostType < mp.memoryTypeCount &&
             (!(req.memoryTypeBits & (1u << hostType)) ||
              (mp.memoryTypes[hostType].propertyFlags & required) != required))
        ++hostType;
      if (hostType == mp.memoryTypeCount)
        throw std::runtime_error("No coherent vertex memory");
      ma.allocationSize = req.size;
      ma.memoryTypeIndex = hostType;
      check(vkAllocateMemory(p.device, &ma, nullptr, &slot.vertexMemory),
            "vertex memory");
      check(vkBindBufferMemory(p.device, slot.vertices, slot.vertexMemory, 0),
            "bind vertices");
      check(vkMapMemory(p.device, slot.vertexMemory, 0, VK_WHOLE_SIZE, 0,
                        &slot.mapped),
            "map vertices");
    }
    VkShaderModuleCreateInfo shader{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = p.particles ? sizeof(particles_vert) : (p.basic ? sizeof(basic_vert) : sizeof(scene_vert));
    shader.pCode = p.particles ? particles_vert : (p.basic ? basic_vert : scene_vert);
    check(vkCreateShaderModule(p.device, &shader, nullptr, &p.vertexShader),
          "vertex shader");
    shader.codeSize = p.particles ? sizeof(particles_frag) : (p.basic ? sizeof(basic_frag) : sizeof(scene_frag));
    shader.pCode = p.particles ? particles_frag : (p.basic ? basic_frag : scene_frag);
    check(vkCreateShaderModule(p.device, &shader, nullptr, &p.fragmentShader),
          "fragment shader");
    VkPushConstantRange push{
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo layout{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(p.device, &layout, nullptr, &p.layout),
          "pipeline layout");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto &stage : stages) {
      stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = p.vertexShader;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p.fragmentShader;
    VkVertexInputBindingDescription binding{0, sizeof(SceneVertex),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, color)}};
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, float(p.extent.width), float(p.extent.height),
                        0, 1};
    VkRect2D scissor{{0, 0}, p.extent};
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo pipeline{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertexInput;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewportState;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples;
    pipeline.pDepthStencilState = &depth;
    pipeline.pColorBlendState = &colorBlend;
    pipeline.layout = p.layout;
    pipeline.renderPass = p.pass;
    check(vkCreateGraphicsPipelines(p.device, VK_NULL_HANDLE, 1, &pipeline,
                                    nullptr, &p.pipeline),
          "mesh pipeline");
    if (!p.basic) {
      // Two small bloom targets plus the final tone-mapped target.
      auto simplePass = [&](VkFormat format, VkImageLayout finalLayout,
                            VkRenderPass &result) {
        VkAttachmentDescription attachment = ad;
        attachment.format = format;
        attachment.finalLayout = finalLayout;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkSubpassDescription subpass = sp;
        subpass.pDepthStencilAttachment = nullptr;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                       VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[0].srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                       VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        auto info = rp;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.pSubpasses = &subpass;
        info.pDependencies = dependencies;
        check(vkCreateRenderPass(p.device, &info, nullptr, &result),
              "post render pass");
      };
      simplePass(VK_FORMAT_R16G16B16A16_SFLOAT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, p.bloomPass);
      simplePass(p.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p.finalPass);
      auto postFramebuffer = [&](VkRenderPass pass, VkImageView view,
                                 VkExtent2D extent, VkFramebuffer &result) {
        auto info = fb;
        info.renderPass = pass;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = extent.width;
        info.height = extent.height;
        check(vkCreateFramebuffer(p.device, &info, nullptr, &result),
              "post framebuffer");
      };
      for (int i = 0; i < 2; ++i)
        postFramebuffer(p.bloomPass, p.bloom[i].view, p.bloomExtent,
                        p.bloomFramebuffer[i]);
      postFramebuffer(p.finalPass, p.view, p.extent, p.finalFramebuffer);
      VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
      sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      check(vkCreateSampler(p.device, &sampler, nullptr, &p.sampler),
            "post sampler");
      VkDescriptorSetLayoutBinding bindings[2]{};
      for (int i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      }
      VkDescriptorSetLayoutCreateInfo ds{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      ds.bindingCount = 2;
      ds.pBindings = bindings;
      check(vkCreateDescriptorSetLayout(p.device, &ds, nullptr,
                                        &p.descriptorLayout),
            "post descriptors");
      VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    10};
      VkDescriptorPoolCreateInfo poolInfo{
          VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      poolInfo.maxSets = 5;
      poolInfo.poolSizeCount = 1;
      poolInfo.pPoolSizes = &poolSize;
      check(vkCreateDescriptorPool(p.device, &poolInfo, nullptr,
                                   &p.descriptorPool),
            "post descriptor pool");
      VkDescriptorSetLayout layouts[] = {p.descriptorLayout, p.descriptorLayout,
                                         p.descriptorLayout, p.descriptorLayout,
                                         p.descriptorLayout};
      VkDescriptorSetAllocateInfo alloc{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      alloc.descriptorPool = p.descriptorPool;
      alloc.descriptorSetCount = 5;
      alloc.pSetLayouts = layouts;
      check(vkAllocateDescriptorSets(p.device, &alloc, p.descriptorSets),
            "post descriptor sets");
      for (int pass = 0; !p.basic && pass < 5; ++pass) {
        VkDescriptorImageInfo images[2] = {
            {p.sampler,
             pass > 0 && pass < 4 ? p.bloom[(pass - 1) % 2].view : p.hdr.view,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {p.sampler, pass > 0 ? p.bloom[(pass - 1) % 2].view : p.hdr.view,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[2]{};
        for (int i = 0; i < 2; ++i) {
          writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          writes[i].dstSet = p.descriptorSets[pass];
          writes[i].dstBinding = i;
          writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          writes[i].descriptorCount = 1;
          writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(p.device, 2, writes, 0, nullptr);
      }
      shader.codeSize = sizeof(post_vert);
      shader.pCode = post_vert;
      check(vkCreateShaderModule(p.device, &shader, nullptr, &p.postVertex),
            "post vertex");
      shader.codeSize = sizeof(post_frag);
      shader.pCode = post_frag;
      check(vkCreateShaderModule(p.device, &shader, nullptr, &p.postFragment),
            "post fragment");
      VkPushConstantRange postPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64};
      layout.setLayoutCount = 1;
      layout.pSetLayouts = &p.descriptorLayout;
      layout.pPushConstantRanges = &postPush;
      check(vkCreatePipelineLayout(p.device, &layout, nullptr, &p.postLayout),
            "post layout");
      stages[0].module = p.postVertex;
      stages[1].module = p.postFragment;
      vertexInput.vertexBindingDescriptionCount =
          vertexInput.vertexAttributeDescriptionCount = 0;
      depth.depthTestEnable = depth.depthWriteEnable = VK_FALSE;
      VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                        VK_DYNAMIC_STATE_SCISSOR};
      VkPipelineDynamicStateCreateInfo dynamic{
          VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
      dynamic.dynamicStateCount = 2;
      dynamic.pDynamicStates = dynamicStates;
      pipeline.pDynamicState = &dynamic;
      pipeline.layout = p.postLayout;
      pipeline.renderPass = p.bloomPass;
      check(vkCreateGraphicsPipelines(p.device, VK_NULL_HANDLE, 1, &pipeline,
                                      nullptr, &p.bloomPipeline),
            "bloom pipeline");
      pipeline.renderPass = p.finalPass;
      check(vkCreateGraphicsPipelines(p.device, VK_NULL_HANDLE, 1, &pipeline,
                                      nullptr, &p.finalPipeline),
            "tone map pipeline");
    }
    if (p.timestampBits && config.enableGpuTiming && !p.basic) {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(p.physical, &props);
      p.timestampPeriod = props.limits.timestampPeriod;
      VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      query.queryType = VK_QUERY_TYPE_TIMESTAMP;
      query.queryCount = 5 * p.slots.size();
      check(vkCreateQueryPool(p.device, &query, nullptr, &p.timestamps),
            "timestamp queries");
    }
    if(p.generator){
      VulkanFrameGenerationConfig fg{p.instance,p.physical,p.device,uint32_t(p.slots.size()),
        p.displayExtent,p.extent,p.format,p.depthFormat,
        p.upscaler?p.upscaler->outputImage():p.offscreen,p.depthImage,
        p.upscaler?p.upscaler->outputView():p.view,p.depthView};
      if(!p.generator->initialize(fg)){p.generationMessage=p.generator->status();p.generator.reset();}
    }
    if (p.surface && p.slots.size() == 2)
      p.acceleration += " · CPU/GPU 双帧流水线";
    return true;
  } catch (...) {
    shutdown();
    throw;
  }
}
FrameHandle VulkanRenderBackend::submitFrame(const RenderFrameInput &in) {
  auto &p = *p_;
  auto &slot = p.slots[p.serial % p.slots.size()];
  const uint32_t queryBase = 5 * (p.serial % p.slots.size());
  if (slot.serial && (!isFrameComplete({slot.serial}) ||
      (p.surface && !isPresentationComplete({slot.serial}))))
    throw std::runtime_error("Offscreen frame still in flight");
  if (p.surface && p.serial != p.lastPresented)
    throw std::runtime_error("Queue presentation before the next scene submission");
  slot.presentSubmitted = false;
  slot.generated = slot.intermediatePresented = false;
  if(p.generator && p.generationWasRunning && !in.allowFrameGeneration){
    check(vkDeviceWaitIdle(p.device),"reset interpolation history");
    p.generator->resetHistory();
  }
  p.generationWasRunning=in.allowFrameGeneration;
  const auto &mesh =
      (p.basic && !p.particles) ? p.basicGeometry.build(in, p.extent.width, p.extent.height)
              : p.geometry.build(in);
  if (mesh.size() > SceneMesh::maxVertices)
    throw std::runtime_error("Scene vertex capacity exceeded");
  std::memcpy(slot.mapped, mesh.data(), mesh.size() * sizeof(SceneVertex));
  check(vkResetFences(p.device, 1, &slot.drawFence), "reset fence");
  check(vkResetCommandBuffer(slot.draw, 0), "reset draw");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  check(vkBeginCommandBuffer(slot.draw, &begin), "begin draw");
  VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  reuse.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  reuse.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  vkCmdPipelineBarrier(slot.draw, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &reuse, 0,
                       nullptr, 0, nullptr);
  if (p.timestamps) {
    vkCmdResetQueryPool(slot.draw, p.timestamps, queryBase, 5);
    vkCmdWriteTimestamp(slot.draw, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        p.timestamps, queryBase);
  }
  VkClearValue clear[2]{};
  clear[0].color = {{0.f, 0.f, 0.f, 1.f}};
  clear[1].depthStencil = {1.f, 0};
  VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rb.renderPass = p.pass;
  rb.framebuffer = p.framebuffer;
  rb.renderArea.extent = p.extent;
  rb.clearValueCount = 2;
  rb.pClearValues = clear;
  vkCmdBeginRenderPass(slot.draw, &rb, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(slot.draw, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(slot.draw, 0, 1, &slot.vertices, &offset);
  auto matrix =
      cameraMatrix(in.camera, float(p.extent.width) / p.extent.height);
  struct ScenePush {
    Matrix4 matrix;
    float params[4];
    float lights[12]{};
  } scenePush{matrix,
              {float(in.state.simulationTime), in.stellarBrightness, 0, 0}};
  for(int i=0;i<3;++i){auto b=in.state.body[i];scenePush.lights[i*4]=float(b.position.x);scenePush.lights[i*4+1]=float(b.position.y);scenePush.lights[i*4+2]=float(b.position.z);scenePush.lights[i*4+3]=float(std::pow(b.mass,3.5));}
  static_assert(sizeof(ScenePush) == 128, "Scene push layout");
  vkCmdPushConstants(slot.draw, p.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(scenePush), &scenePush);
  vkCmdDraw(slot.draw, mesh.size(), 1, 0, 0);
  vkCmdEndRenderPass(slot.draw);
  if (p.timestamps)
    vkCmdWriteTimestamp(slot.draw, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        p.timestamps, queryBase + 1);
  for (int pass = 0; !p.basic && pass < 5; ++pass) {
    bool final = pass == 4;
    auto extent = final ? p.extent : p.bloomExtent;
    VkRenderPassBeginInfo post{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    post.renderPass = final ? p.finalPass : p.bloomPass;
    post.framebuffer =
        final ? p.finalFramebuffer : p.bloomFramebuffer[pass % 2];
    post.renderArea.extent = extent;
    vkCmdBeginRenderPass(slot.draw, &post, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(slot.draw, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      final ? p.finalPipeline : p.bloomPipeline);
    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(slot.draw, 0, 1, &viewport);
    vkCmdSetScissor(slot.draw, 0, 1, &scissor);
    vkCmdBindDescriptorSets(slot.draw, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            p.postLayout, 0, 1, &p.descriptorSets[pass], 0,
                            nullptr);
    bool srgb = p.format == VK_FORMAT_R8G8B8A8_SRGB ||
                p.format == VK_FORMAT_B8G8R8A8_SRGB;
    struct PostPush {
      float params[4];
      std::array<float, 12> halos;
    };
    static_assert(sizeof(PostPush) == 64, "Post push layout");
    PostPush params{{float(pass), srgb ? 0.f : 1.f, float(p.workloadLevel),
                     in.stellarBrightness},
                    stellarHalos(in, matrix, true)};
    vkCmdPushConstants(slot.draw, p.postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(params), &params);
    vkCmdDraw(slot.draw, 3, 1, 0, 0);
    vkCmdEndRenderPass(slot.draw);
    if (p.timestamps && pass < 2)
      vkCmdWriteTimestamp(slot.draw, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          p.timestamps, queryBase + (final ? 4 : 2 + pass));
  }
  if (p.upscaler) {
    if (p.upscaledImage)
      p.barrier(slot.draw, p.upscaledImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    p.barrier(slot.draw, p.offscreen, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    p.upscaledImage = p.upscaler->render(slot.draw, p.view);
    p.barrier(slot.draw, p.upscaledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  if(p.generator && in.allowFrameGeneration){
    p.generatedImage=p.generator->dispatch(slot.draw,uint32_t(p.serial%p.slots.size()),matrix);
    slot.generated=p.generatedImage!=VK_NULL_HANDLE;
  }
  if (p.timestamps && !p.basic)
    vkCmdWriteTimestamp(slot.draw, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, p.timestamps, queryBase + 4);
  check(vkEndCommandBuffer(slot.draw), "end draw");
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &slot.draw;
  check(vkQueueSubmit(p.queue, 1, &si, slot.drawFence), "submit offscreen");
  slot.serial = ++p.serial;
  return {slot.serial};
}
bool VulkanRenderBackend::isFrameComplete(FrameHandle h) {
  auto &p = *p_;
  if (!h.value || h.value > p.serial)
    return false;
  if (h.value <= p.completed)
    return true;
  auto &slot = p.slots[(h.value - 1) % p.slots.size()];
  if (slot.serial != h.value)
    return false;
  auto result = vkGetFenceStatus(p.device, slot.drawFence);
  if (result == VK_NOT_READY)
    return false;
  check(result, "offscreen fence");
  p.completed = std::max(p.completed, h.value);
  return true;
}
bool VulkanRenderBackend::presentIntermediateFrame(FrameHandle h) {
  auto& p=*p_;
  if(!p.swap || !h.value || h.value!=p.serial) return false;
  auto& slot=p.slots[(h.value-1)%p.slots.size()];
  if(!slot.generated) return false;
  if(slot.intermediatePresented) throw std::runtime_error("Duplicate intermediate presentation");
  presentImage(h,true); slot.intermediatePresented=true; return true;
}
void VulkanRenderBackend::presentFrame(FrameHandle h) { presentImage(h,false); }
void VulkanRenderBackend::presentImage(FrameHandle h,bool intermediate) {
  auto &p = *p_;
  if (!p.swap)
    return;
  if (!h.value || h.value != p.serial || h.value == p.lastPresented)
    throw std::runtime_error("Invalid or duplicate presentation");
  auto &slot = p.slots[(h.value - 1) % p.slots.size()];
  auto command=intermediate?slot.intermediateCopy:slot.presentCopy;
  auto acquire=intermediate?slot.intermediateAcquired:slot.acquired;
  uint32_t index;
  auto result = vkAcquireNextImageKHR(p.device, p.swap, 1000000000, acquire,
                                      VK_NULL_HANDLE, &index);
  if (result != VK_SUBOPTIMAL_KHR)
    check(result, "acquire preview");
  check(vkResetCommandBuffer(command, 0), "reset copy");
  if(!intermediate) check(vkResetFences(p.device, 1, &slot.presentFence), "reset copy fence");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  check(vkBeginCommandBuffer(command, &begin), "begin preview");
  p.barrier(command, p.images[index], VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
  if (p.upscaler) {
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = blit.dstOffsets[1] = {int32_t(p.displayExtent.width), int32_t(p.displayExtent.height), 1};
    // Same size: this only converts RGBA to the surface format when necessary.
    vkCmdBlitImage(command, intermediate?p.generatedImage:p.upscaledImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p.images[index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
  } else {
  VkImageCopy region{};
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstSubresource = region.srcSubresource;
  region.extent = {p.extent.width, p.extent.height, 1};
  vkCmdCopyImage(command, intermediate?p.generatedImage:p.offscreen, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 p.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                 &region);
  }
  p.barrier(command, p.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  check(vkEndCommandBuffer(command), "end preview");
  VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &acquire;
  si.pWaitDstStageMask = &wait;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &command;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &p.presented[index];
  check(vkQueueSubmit(p.queue, 1, &si, intermediate?VK_NULL_HANDLE:slot.presentFence), "submit preview");
  if(!intermediate){slot.presentSubmitted = true; p.lastPresented = h.value;}
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &p.presented[index];
  pi.swapchainCount = 1;
  pi.pSwapchains = &p.swap;
  pi.pImageIndices = &index;
  VkPresentTimeGOOGLE presentTime{++p.presentId,0};
  VkPresentTimesInfoGOOGLE times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
  times.swapchainCount=1; times.pTimes=&presentTime;
  if(p.pastTiming) pi.pNext=&times;
  result = vkQueuePresentKHR(p.queue, &pi);
  if (result != VK_SUBOPTIMAL_KHR)
    check(result, "present preview");
  // Queue order and the scene's full memory barrier protect shared HDR/bloom
  // targets. Do not stall the CPU here: it can now prepare the next slot.

}
bool VulkanRenderBackend::isPresentationComplete(FrameHandle h) {
  auto &p = *p_;
  if (!p.surface) return isFrameComplete(h);
  if (!h.value || h.value > p.lastPresented) return false;
  if (h.value <= p.presentationCompleted) return true;
  auto &slot = p.slots[(h.value - 1) % p.slots.size()];
  if (slot.serial != h.value || !slot.presentSubmitted) return false;
  auto result = vkGetFenceStatus(p.device, slot.presentFence);
  if (result == VK_NOT_READY) return false;
  check(result, "presentation copy fence");
  p.presentationCompleted = std::max(p.presentationCompleted, h.value);
  return true;
}
DisplayTiming VulkanRenderBackend::pollDisplayTiming() {
  auto& p=*p_;
  if(!p.pastTiming || !p.swap) return {};
  uint32_t count=0;
  if(p.pastTiming(p.device,p.swap,&count,nullptr)!=VK_SUCCESS) return {};
  if(count) {
    std::vector<VkPastPresentationTimingGOOGLE> records(count);
    auto result=p.pastTiming(p.device,p.swap,&count,records.data());
    if(result!=VK_SUCCESS && result!=VK_INCOMPLETE) return {};
    records.resize(count);
    std::sort(records.begin(),records.end(),[](const auto& a,const auto& b){return a.actualPresentTime<b.actualPresentTime;});
    for(const auto& t:records) if(t.actualPresentTime && (p.displayTimes.empty() || t.actualPresentTime>p.displayTimes.back())) {
      p.displayTimes.push_back(t.actualPresentTime);
      p.timingArrival=std::chrono::steady_clock::now();
    }
    while(p.displayTimes.size()>2 && p.displayTimes.back()-p.displayTimes[1]>1000000000ULL) p.displayTimes.pop_front();
  }
  if(p.displayTimes.size()<2) return {true,-1};
  if(std::chrono::steady_clock::now()-p.timingArrival>std::chrono::seconds(1)) return {true,-1};
  auto duration=p.displayTimes.back()-p.displayTimes.front();
  if(duration<500000000ULL) return {true,-1};
  return {true,1e9*double(p.displayTimes.size()-1)/double(duration)};
}
void VulkanRenderBackend::shutdown() {
  auto &p = *p_;
  if (p.device) {
    vkDeviceWaitIdle(p.device);
    p.generator.reset();
    p.upscaler.reset();
    if (p.bloomPipeline)
      vkDestroyPipeline(p.device, p.bloomPipeline, nullptr);
    if (p.finalPipeline)
      vkDestroyPipeline(p.device, p.finalPipeline, nullptr);
    if (p.postLayout)
      vkDestroyPipelineLayout(p.device, p.postLayout, nullptr);
    if (p.postVertex)
      vkDestroyShaderModule(p.device, p.postVertex, nullptr);
    if (p.postFragment)
      vkDestroyShaderModule(p.device, p.postFragment, nullptr);
    if (p.descriptorPool)
      vkDestroyDescriptorPool(p.device, p.descriptorPool, nullptr);
    if (p.descriptorLayout)
      vkDestroyDescriptorSetLayout(p.device, p.descriptorLayout, nullptr);
    if (p.sampler)
      vkDestroySampler(p.device, p.sampler, nullptr);
    for (auto framebuffer : p.bloomFramebuffer)
      if (framebuffer)
        vkDestroyFramebuffer(p.device, framebuffer, nullptr);
    if (p.finalFramebuffer)
      vkDestroyFramebuffer(p.device, p.finalFramebuffer, nullptr);
    if (p.bloomPass)
      vkDestroyRenderPass(p.device, p.bloomPass, nullptr);
    if (p.finalPass)
      vkDestroyRenderPass(p.device, p.finalPass, nullptr);
    if (p.timestamps)
      vkDestroyQueryPool(p.device, p.timestamps, nullptr);
    if (p.pipeline)
      vkDestroyPipeline(p.device, p.pipeline, nullptr);
    if (p.layout)
      vkDestroyPipelineLayout(p.device, p.layout, nullptr);
    if (p.vertexShader)
      vkDestroyShaderModule(p.device, p.vertexShader, nullptr);
    if (p.fragmentShader)
      vkDestroyShaderModule(p.device, p.fragmentShader, nullptr);
    for (auto &slot : p.slots) {
      if (slot.mapped)
        vkUnmapMemory(p.device, slot.vertexMemory);
      if (slot.vertices)
        vkDestroyBuffer(p.device, slot.vertices, nullptr);
      if (slot.vertexMemory)
        vkFreeMemory(p.device, slot.vertexMemory, nullptr);
      if (slot.drawFence)
        vkDestroyFence(p.device, slot.drawFence, nullptr);
      if (slot.presentFence) vkDestroyFence(p.device, slot.presentFence, nullptr);
      if (slot.acquired) vkDestroySemaphore(p.device, slot.acquired, nullptr);
      if (slot.intermediateAcquired) vkDestroySemaphore(p.device, slot.intermediateAcquired, nullptr);
    }
    for (auto s : p.presented)
      if (s)
        vkDestroySemaphore(p.device, s, nullptr);
    if (p.acquired)
      vkDestroySemaphore(p.device, p.acquired, nullptr);
    if (p.copyFence)
      vkDestroyFence(p.device, p.copyFence, nullptr);
    if (p.pool)
      vkDestroyCommandPool(p.device, p.pool, nullptr);
    if (p.framebuffer)
      vkDestroyFramebuffer(p.device, p.framebuffer, nullptr);
    if (p.pass)
      vkDestroyRenderPass(p.device, p.pass, nullptr);
    for (auto target : {p.hdr, p.bloom[0], p.bloom[1]}) {
      if (target.view)
        vkDestroyImageView(p.device, target.view, nullptr);
      if (target.image)
        vkDestroyImage(p.device, target.image, nullptr);
      if (target.memory)
        vkFreeMemory(p.device, target.memory, nullptr);
    }
    if (p.depthView)
      vkDestroyImageView(p.device, p.depthView, nullptr);
    if (p.depthImage)
      vkDestroyImage(p.device, p.depthImage, nullptr);
    if (p.depthMemory)
      vkFreeMemory(p.device, p.depthMemory, nullptr);
    if (p.view)
      vkDestroyImageView(p.device, p.view, nullptr);
    if (p.offscreen)
      vkDestroyImage(p.device, p.offscreen, nullptr);
    if (p.memory)
      vkFreeMemory(p.device, p.memory, nullptr);
    if (p.swap)
      vkDestroySwapchainKHR(p.device, p.swap, nullptr);
    vkDestroyDevice(p.device, nullptr);
  }
  if (p.surface)
    vkDestroySurfaceKHR(p.instance, p.surface, nullptr);
  if (p.instance)
    vkDestroyInstance(p.instance, nullptr);
  void *w = p.window;
  p_.reset(new Impl(w));
}
BackendCapabilities VulkanRenderBackend::queryCapabilities() const {
  return {GraphicsBackendType::Vulkan, true, p_->timestamps != VK_NULL_HANDLE,
          p_->window != nullptr, p_->window != nullptr && p_->slots.size() > 1};
}
GpuTiming VulkanRenderBackend::getGpuTiming(FrameHandle h) {
  auto &p = *p_;
  if (!p.timestamps || !h.value ||
      p.slots[(h.value - 1) % p.slots.size()].serial != h.value ||
      !isFrameComplete(h))
    return {};
  uint64_t values[5]{};
  check(vkGetQueryPoolResults(
            p.device, p.timestamps, 5 * ((h.value - 1) % p.slots.size()), 5,
            sizeof(values), values, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT),
        "timestamp results");
  uint64_t mask = p.timestampBits == 64 ? ~uint64_t(0)
                                        : (uint64_t(1) << p.timestampBits) - 1;
  GpuTiming result{true, double((values[4] - values[0]) & mask) *
                             p.timestampPeriod / 1e6};
  result.stagesValid = true;
  for (int i = 0; i < 4; ++i)
    result.stageMs[i] =
        double((values[i + 1] - values[i]) & mask) * p.timestampPeriod / 1e6;
  return result;
}
void VulkanRenderBackend::resizePreview(uint32_t, uint32_t) {
  throw std::runtime_error("Preview resize requires surface recreation");
}
#ifdef THREEBODY_RENDER_TESTING
std::vector<uint8_t> VulkanRenderBackend::readbackForTest(bool generated) {
  auto &p = *p_;
  if (p.window || !p.serial || !isFrameComplete({p.serial}))
    throw std::runtime_error("Readback requires completed headless frame");
  if(generated && !p.generatedImage) throw std::runtime_error("No generated frame");
  const VkDeviceSize bytes = VkDeviceSize(p.displayExtent.width) * p.displayExtent.height * 4;
  VkBuffer buffer{};
  VkDeviceMemory memory{};
  auto release = [&] {
    if (buffer)
      vkDestroyBuffer(p.device, buffer, nullptr);
    if (memory)
      vkFreeMemory(p.device, memory, nullptr);
  };
  try {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(p.device, &info, nullptr, &buffer), "readback buffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(p.device, buffer, &req);
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(p.physical, &props);
    uint32_t type = 0;
    auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < props.memoryTypeCount &&
           (!(req.memoryTypeBits & (1u << type)) ||
            (props.memoryTypes[type].propertyFlags & flags) != flags))
      ++type;
    if (type == props.memoryTypeCount)
      throw std::runtime_error("No readback memory");
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    check(vkAllocateMemory(p.device, &alloc, nullptr, &memory),
          "readback memory");
    check(vkBindBufferMemory(p.device, buffer, memory, 0), "readback bind");
    check(vkResetCommandBuffer(p.copy, 0), "readback reset");
    check(vkResetFences(p.device, 1, &p.copyFence), "readback fence reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(p.copy, &begin), "readback begin");
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {p.displayExtent.width, p.displayExtent.height, 1};
    vkCmdCopyImageToBuffer(p.copy, generated?p.generatedImage:(p.upscaledImage ? p.upscaledImage : p.offscreen),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                           &copy);
    check(vkEndCommandBuffer(p.copy), "readback end");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &p.copy;
    check(vkQueueSubmit(p.queue, 1, &submit, p.copyFence), "readback submit");
    check(vkWaitForFences(p.device, 1, &p.copyFence, VK_TRUE, 10000000000),
          "readback wait");
    void *mapped{};
    check(vkMapMemory(p.device, memory, 0, bytes, 0, &mapped), "readback map");
    std::vector<uint8_t> pixels(bytes);
    std::memcpy(pixels.data(), mapped, bytes);
    vkUnmapMemory(p.device, memory);
    release();
    return pixels;
  } catch (...) {
    vkDeviceWaitIdle(p.device);
    p.upscaler.reset();
    release();
    throw;
  }
}
#endif
} // namespace threebody
