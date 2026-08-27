#ifndef VULKAN_H
#define VULKAN_H

#include "Types.h"
#include "Memory.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "EngaFormat.h"
#include "EngineMath.h"

#include <vulkan/vulkan.h>

#include "shaders/ShaderInterop.h"
#include "VulkanRender.h"

static_assert(sizeof(vertex) == sizeof(enga_vertex), "vertex must match the packed asset layout");
static_assert(sizeof(vertex) == VERTEX_STRIDE, "vertex stride must match the shader stride");
static_assert(sizeof(gpu_material) == MATERIAL_STRIDE, "material stride must match the shader stride");
static_assert(sizeof(rect_params) == RECT_PARAMS_STRIDE, "rect params stride must match the shader stride");
static_assert(sizeof(voxelize_params) == 144, "voxelize params must match the shader layout");

#define MAX_SURFACE_FORMATS   64
#define MAX_PRESENT_MODES     8

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

struct queue_family_indices
{
    uint32 graphicsIndex;
    uint32 presentIndex;
    bool32 graphicsSupported;
    bool32 presentSupported;
};

struct swapchain_support_details
{
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR formats[MAX_SURFACE_FORMATS];
    uint32 formatCount;
    VkPresentModeKHR presentModes[MAX_PRESENT_MODES];
    uint32 presentModeCount;
};

internal const char *InitVulkan(HINSTANCE hinstance, HWND hwnd);
internal void RenderVulkanFrame(render_commands *Commands);

#endif
