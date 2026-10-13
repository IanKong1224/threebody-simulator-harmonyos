#include "GraphicsFrameGenerator.h"
#include <graphics_game_sdk/frame_generation_vk.h>
#include <syscap_ndk.h>
#include <dlfcn.h>
#include <cmath>
#include <algorithm>
namespace threebody {
namespace {
// Double intermediates avoid avoidable loss when inverting the projection.
bool inverse(const Matrix4& m, FG_Mat4x4& out) {
  double a[4][8]{};
  for(int r=0;r<4;++r) for(int c=0;c<4;++c) { a[r][c]=m[c*4+r]; a[r][c+4]=r==c; }
  for(int c=0;c<4;++c) {
    int p=c; for(int r=c+1;r<4;++r) if(std::abs(a[r][c])>std::abs(a[p][c])) p=r;
    if(std::abs(a[p][c])<1e-15) return false;
    for(int j=0;j<8;++j) std::swap(a[p][j],a[c][j]);
    double d=a[c][c]; for(int j=0;j<8;++j)a[c][j]/=d;
    for(int r=0;r<4;++r) if(r!=c){ double f=a[r][c];for(int j=0;j<8;++j)a[r][j]-=f*a[c][j]; }
  }
  for(int r=0;r<4;++r)for(int c=0;c<4;++c)out.data[c*4+r]=float(a[r][c+4]);
  return true;
}
class GraphicsFrameGenerator final : public VulkanFrameGenerator {
  void* lib_{}; VkDevice device_{}; FG_Context_VK* context_{};
  FG_Image_VK *color_{}, *depth_{}, *output_{};
  VkImage image_{}; VkImageView view_{}; VkDeviceMemory memory_{};
  bool active_{false}, written_{false};
  std::string message_{"Graphics 超帧尚未初始化"};
#define FN(name) decltype(&HMS_FG_##name##_VK) name##_{};
  FN(CreateContext) FN(DestroyContext) FN(SetAlgorithmMode) FN(SetResolution)
  FN(SetImageFormat) FN(SetCvvZSemantic) FN(SetIntegrationMode)
  FN(CreateImage) FN(DestroyImage) FN(Activate) FN(Deactivate) FN(Dispatch)
#undef FN
  bool error(FG_ErrorCode code, const char* where) {
    if(code==FG_SUCCESS)return false;
    active_=false;message_=std::string("Graphics 超帧回退：")+where+" ("+std::to_string(code)+")；真实渲染目标 120";return true;
  }
public:
  ~GraphicsFrameGenerator() override {
    // Backend waits idle first, including all SDK-recorded commands.
    if(context_) {
      if(Deactivate_)Deactivate_(context_);
      if(DestroyImage_)for(auto image:{color_,depth_,output_})if(image)DestroyImage_(context_,image);
      if(DestroyContext_)DestroyContext_(&context_);
    }
    if(view_)vkDestroyImageView(device_,view_,nullptr);
    if(image_)vkDestroyImage(device_,image_,nullptr);
    if(memory_)vkFreeMemory(device_,memory_,nullptr);
    if(lib_)dlclose(lib_);
  }
  bool active() const override { return active_; }
  std::string status() const override { return message_; }
  bool initialize(const VulkanFrameGenerationConfig& c) override {
    message_="设备或格式不支持 Graphics 超帧；真实渲染目标 120";
    if(!canIUse("SystemCapability.GraphicsGame.RenderAccelerate"))return false;
    if(c.slots<1||c.slots>2||!c.colorView||!c.depthView)return false;
    // Avoid implicit sRGB storage conversion in SDK-generated output.
    if(c.colorFormat!=VK_FORMAT_R8G8B8A8_UNORM && c.colorFormat!=VK_FORMAT_B8G8R8A8_UNORM)return false;
    lib_=dlopen("libframegeneration.so",RTLD_NOW|RTLD_LOCAL); if(!lib_)return false;
#define LOAD(name) name##_=reinterpret_cast<decltype(name##_)>(dlsym(lib_,"HMS_FG_" #name "_VK")); if(!name##_)return false;
    LOAD(CreateContext) LOAD(DestroyContext) LOAD(SetAlgorithmMode) LOAD(SetResolution)
    LOAD(SetImageFormat) LOAD(SetCvvZSemantic) LOAD(SetIntegrationMode)
    LOAD(CreateImage) LOAD(DestroyImage) LOAD(Activate) LOAD(Deactivate) LOAD(Dispatch)
#undef LOAD
    device_=c.device;
    FG_ContextDescription_VK desc{c.instance,c.physical,c.device,uint8_t(c.slots),vkGetInstanceProcAddr};
    context_=CreateContext_(&desc); if(!context_)return false;
    FG_AlgorithmModeInfo mode{FG_PREDICTION_MODE_INTERPOLATION,FG_ME_MODE_BASIC};
    FG_ResolutionInfo resolution{{c.colorExtent.width,c.colorExtent.height},{c.depthExtent.width,c.depthExtent.height},{c.colorExtent.width,c.colorExtent.height}};
    FG_ImageFormat_VK format{c.colorFormat,c.depthFormat,c.colorFormat};
    FG_IntegrationInfo integration{FG_PRESENT_BY_GAME,false,false,false};
    if(error(SetAlgorithmMode_(context_,&mode),"算法")||error(SetResolution_(context_,&resolution),"分辨率")||
       error(SetImageFormat_(context_,&format),"格式")||error(SetCvvZSemantic_(context_,FG_CVV_Z_SEMANTIC_ZERO_TO_ONE_FORWARD_Z),"深度")||
       error(SetIntegrationMode_(context_,&integration),"送显模式"))return false;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
    info.format=c.colorFormat;info.extent={c.colorExtent.width,c.colorExtent.height,1};info.mipLevels=info.arrayLayers=1;
    info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(vkCreateImage(device_,&info,nullptr,&image_)!=VK_SUCCESS)return false;
    VkMemoryRequirements req{};vkGetImageMemoryRequirements(device_,image_,&req);
    VkPhysicalDeviceMemoryProperties mp{};vkGetPhysicalDeviceMemoryProperties(c.physical,&mp);
    uint32_t i=0;while(i<mp.memoryTypeCount&&(!(req.memoryTypeBits&(1u<<i))||!(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++i;
    if(i==mp.memoryTypeCount)return false;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=i;
    if(vkAllocateMemory(device_,&alloc,nullptr,&memory_)!=VK_SUCCESS||vkBindImageMemory(device_,image_,memory_,0)!=VK_SUCCESS)return false;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image_;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=c.colorFormat;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if(vkCreateImageView(device_,&vi,nullptr,&view_)!=VK_SUCCESS)return false;
    color_=CreateImage_(context_,c.color,c.colorView);depth_=CreateImage_(context_,c.depth,c.depthView);output_=CreateImage_(context_,image_,view_);
    if(!color_||!depth_||!output_||error(Activate_(context_),"激活"))return false;
    active_=true;message_="Graphics 超帧已激活 · 收集历史帧";return true;
  }
  void resetHistory() override {
    if(!active_)return;
    if(error(Deactivate_(context_),"清空历史")||error(Activate_(context_),"重新激活"))return;
    message_="Graphics 超帧已激活 · 收集历史帧";
  }
  VkImage dispatch(VkCommandBuffer cmd,uint32_t slot,const Matrix4& matrix) override {
    if(!active_)return VK_NULL_HANDLE;
    FG_DispatchDescription_VK d{};
    d.inputColorInfo={color_,{VK_ACCESS_MEMORY_WRITE_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT},
                             {VK_ACCESS_TRANSFER_READ_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT}};
    d.inputDepthStencilInfo={depth_,{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT},
                                     {VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT}};
    Matrix4 vkMatrix=matrix;
    for(int c=0;c<4;++c){vkMatrix[c*4+1]=-matrix[c*4+1];vkMatrix[c*4+2]=.5f*(matrix[c*4+2]+matrix[c*4+3]);}
    std::copy(vkMatrix.begin(),vkMatrix.end(),d.viewProj.data);
    if(!inverse(vkMatrix,d.invViewProj)) {active_=false;message_="超帧相机矩阵无效；已回退";return VK_NULL_HANDLE;}
    // Explicitly transition output first: warm-up must not depend on whether SDK writes it.
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image_;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.oldLayout=written_?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcAccessMask=written_?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:0;b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    d.outputColorInfo={output_,{VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT},
                                {VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT}};
    d.vkCommandBuffer=cmd;d.frameIdx=uint8_t(slot);
    auto code=Dispatch_(context_,&d);
    b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);written_=true;
    if(code==FG_COLLECTING_PREVIOUS_FRAMES){message_="Graphics 超帧已激活 · 收集历史帧";return VK_NULL_HANDLE;}
    if(error(code,"生成"))return VK_NULL_HANDLE;
    message_="Graphics 超帧运行中 · 60 真实帧目标 → 120 送显目标";return image_;
  }
};
}
std::unique_ptr<VulkanFrameGenerator> makeGraphicsFrameGenerator(){return std::unique_ptr<VulkanFrameGenerator>(new GraphicsFrameGenerator);}
}
