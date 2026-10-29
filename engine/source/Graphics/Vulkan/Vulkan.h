#ifndef VULKAN_H
#define VULKAN_H

#include "Types.h"
#include "Memory.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "EngaFormat.h"
#include "EngineMath.h"

#include <vulkan/vulkan.h>

#include "shaders/interop/ShaderResources.h"

#define MAX_SWAPCHAIN_IMAGES  8
#define MAX_FRAMES_IN_FLIGHT  2

static_assert(sizeof(vertex) == sizeof(enga_vertex), "vertex must match the packed asset layout");
static_assert(sizeof(vertex) == VERTEX_STRIDE, "vertex stride must match the shader stride");
static_assert(sizeof(gpu_material_params) == 32, "material params must match the shader layout");
static_assert(offsetof(gpu_material_params, TextureHandles) == 16, "texture address must match the shader layout");
static_assert(offsetof(draw_params, Material) == 64, "material params must follow the model matrix");
static_assert(offsetof(draw_params, Vertices) == 96, "vertex address must match the shader layout");
static_assert(sizeof(draw_params) == 112, "draw params must match the shader layout");
static_assert(sizeof(rect_params) == RECT_PARAMS_STRIDE, "rect params stride must match the shader stride");
static_assert(sizeof(push_constants) == 16, "push constants contain only two GPU addresses");
static_assert(sizeof(frame_globals) == 128, "frame globals must match the shader layout");

struct vulkan_context
{
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    HWND windowHandle;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    uint32 graphicsFamilyIndex;
    uint32 presentFamilyIndex;

    VkQueryPool timestampPool;
    real32      timestampPeriod;

    VkSwapchainKHR swapchain;
    VkImage swapchainImages[MAX_SWAPCHAIN_IMAGES];
    VkImageView swapchainImageViews[MAX_SWAPCHAIN_IMAGES];
    uint32 swapchainImageCount;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];

    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphores[MAX_SWAPCHAIN_IMAGES];
    VkSemaphore frameTimeline;
    uint64      frameIndex;

    PFN_vkCreateShadersEXT                  CreateShadersEXT;
    PFN_vkDestroyShaderEXT                  DestroyShaderEXT;
    PFN_vkCmdBindShadersEXT                 CmdBindShadersEXT;
    PFN_vkCmdSetVertexInputEXT              CmdSetVertexInputEXT;
    PFN_vkCmdSetRasterizationSamplesEXT     CmdSetRasterizationSamplesEXT;
    PFN_vkCmdSetSampleMaskEXT               CmdSetSampleMaskEXT;
    PFN_vkCmdSetAlphaToCoverageEnableEXT    CmdSetAlphaToCoverageEnableEXT;
    PFN_vkCmdSetPolygonModeEXT              CmdSetPolygonModeEXT;
    PFN_vkCmdSetDepthClampEnableEXT         CmdSetDepthClampEnableEXT;
    PFN_vkCmdSetLogicOpEnableEXT            CmdSetLogicOpEnableEXT;
    PFN_vkCmdSetColorBlendEnableEXT         CmdSetColorBlendEnableEXT;
    PFN_vkCmdSetColorBlendEquationEXT       CmdSetColorBlendEquationEXT;
    PFN_vkCmdSetColorWriteMaskEXT           CmdSetColorWriteMaskEXT;

    PFN_vkGetDescriptorSetLayoutSizeEXT          GetDescriptorSetLayoutSizeEXT;
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT GetDescriptorSetLayoutBindingOffsetEXT;
    PFN_vkGetDescriptorEXT                       GetDescriptorEXT;
    PFN_vkCmdBindDescriptorBuffersEXT            CmdBindDescriptorBuffersEXT;
    PFN_vkCmdSetDescriptorBufferOffsetsEXT       CmdSetDescriptorBufferOffsetsEXT;

    VkPhysicalDeviceDescriptorBufferPropertiesEXT DescriptorProps;
    VkPhysicalDeviceMemoryProperties              MemoryProps;
};

#endif
