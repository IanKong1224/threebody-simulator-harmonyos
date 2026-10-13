#pragma once
// Fake upscale for testing FG composition; not the Huawei algorithm.
#include "renderer/vulkan/VulkanRenderBackend.h"
#include "core/physics/InitialConditions.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace threebody;
void require(bool ok) { if (!ok) throw std::runtime_error("upscale contract failure"); }
// Test double only: checks the renderer's scaled path/lifetime/fallback, NOT XEngine quality.
struct Probe : VulkanUpscaler {
  bool fail; VkDevice device{}; VkImage image{}; VkDeviceMemory memory{}; VkImageView view{};
  bool first=true; int& calls; bool& destroyed;
  Probe(bool f,int& c,bool& d):fail(f),calls(c),destroyed(d){}
  ~Probe() { if(view)vkDestroyImageView(device,view,nullptr); if(image)vkDestroyImage(device,image,nullptr); if(memory)vkFreeMemory(device,memory,nullptr);destroyed=true; }
  std::string status() const override { return fail ? "fallback-tested" : "active-tested"; }
  bool initialize(VkPhysicalDevice physical,VkDevice d,VkExtent2D input,VkExtent2D output) override {
    require(input.width==96 && input.height==72 && output.width==128 && output.height==96);
    if(fail)return false; device=d;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
    info.format=VK_FORMAT_R8G8B8A8_UNORM;info.extent={128,96,1};info.mipLevels=info.arrayLayers=1;
    info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    require(vkCreateImage(d,&info,nullptr,&image)==VK_SUCCESS);
    VkMemoryRequirements req{};vkGetImageMemoryRequirements(d,image,&req);
    VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical,&props);
    uint32_t type=0;while(type<props.memoryTypeCount && !(req.memoryTypeBits&(1u<<type)))++type;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
    require(vkAllocateMemory(d,&alloc,nullptr,&memory)==VK_SUCCESS);
    require(vkBindImageMemory(d,image,memory,0)==VK_SUCCESS);VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=image;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.format=info.format;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};require(vkCreateImageView(d,&v,nullptr,&view)==VK_SUCCESS);return true;
  }
  VkImage outputImage()const override{return image;}
  VkImageView outputView()const override{return view;}
  VkImage render(VkCommandBuffer cmd,VkImageView input) override {
    require(input!=VK_NULL_HANDLE);++calls;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    b.oldLayout=first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    VkClearColorValue color{{.25f,.5f,1.f,1.f}};
    vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&b.subresourceRange);
    b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    first=false;return image;
  }
};
