// Test substitute for Huawei's proprietary runtime; never included in a HAP.
#include <graphics_game_sdk/frame_generation_vk.h>
#include <cassert>
#include <cstdlib>
#include <cmath>
struct FG_Context_VK { unsigned frames=0; bool active=false; uint8_t slots=0; };
struct FG_Image_VK { VkImage image; };
extern "C" {
FG_Context_VK* HMS_FG_CreateContext_VK(const FG_ContextDescription_VK* d){if(std::getenv("FG_TEST_UNSUPPORTED"))return nullptr;assert(d->framesInFlight==2);auto c=new FG_Context_VK;c->slots=d->framesInFlight;return c;}
FG_ErrorCode HMS_FG_DestroyContext_VK(FG_Context_VK** c){delete *c;*c=nullptr;return FG_SUCCESS;}
FG_ErrorCode HMS_FG_SetAlgorithmMode_VK(FG_Context_VK*,const FG_AlgorithmModeInfo* m){assert(m->predictionMode==FG_PREDICTION_MODE_INTERPOLATION);return FG_SUCCESS;}
FG_ErrorCode HMS_FG_SetResolution_VK(FG_Context_VK*,const FG_ResolutionInfo* r){assert(r->inputColorResolution.width==128);assert(r->inputDepthStencilResolution.width==128 || r->inputDepthStencilResolution.width==96);return FG_SUCCESS;}
FG_ErrorCode HMS_FG_SetImageFormat_VK(FG_Context_VK*,const FG_ImageFormat_VK* f){assert(f->inputColorFormat==VK_FORMAT_R8G8B8A8_UNORM);return FG_SUCCESS;}
FG_ErrorCode HMS_FG_SetCvvZSemantic_VK(FG_Context_VK*,FG_CvvZSemantic s){assert(s==FG_CVV_Z_SEMANTIC_ZERO_TO_ONE_FORWARD_Z);return FG_SUCCESS;}
FG_ErrorCode HMS_FG_SetIntegrationMode_VK(FG_Context_VK*,const FG_IntegrationInfo* i){assert(i->presentMode==FG_PRESENT_BY_GAME&&!i->textureCachedByGame);return FG_SUCCESS;}
FG_Image_VK* HMS_FG_CreateImage_VK(FG_Context_VK*,VkImage i,VkImageView v){assert(i&&v);return new FG_Image_VK{i};}
FG_ErrorCode HMS_FG_DestroyImage_VK(FG_Context_VK*,FG_Image_VK* i){delete i;return FG_SUCCESS;}
FG_ErrorCode HMS_FG_Activate_VK(FG_Context_VK* c){c->active=true;c->frames=0;return FG_SUCCESS;}
FG_ErrorCode HMS_FG_Deactivate_VK(FG_Context_VK* c){c->active=false;return FG_SUCCESS;}
FG_ErrorCode HMS_FG_Dispatch_VK(FG_Context_VK* c,const FG_DispatchDescription_VK* d){
 assert(c->active && d->frameIdx<c->slots);
 assert(d->inputColorInfo.initialSync.layout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
 assert(d->inputDepthStencilInfo.initialSync.layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
 for(int r=0;r<4;++r)for(int col=0;col<4;++col){double v=0;for(int k=0;k<4;++k)v+=double(d->viewProj.data[k*4+r])*d->invViewProj.data[col*4+k];assert(std::abs(v-(r==col))<1e-4);}
 if(std::getenv("FG_TEST_DISPATCH_FAIL"))return FG_INVALID_PARAMETER;
 if(++c->frames<=3)return FG_COLLECTING_PREVIOUS_FRAMES;
 VkClearColorValue color{{.125f,.5f,.875f,1.f}};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 vkCmdClearColorImage(d->vkCommandBuffer,d->outputColorInfo.image->image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
 return FG_SUCCESS;
}
}
