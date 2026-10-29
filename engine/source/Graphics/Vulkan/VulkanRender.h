#ifndef VULKANRENDER_H
#define VULKANRENDER_H

#include "Types.h"
#include "EngineMath.h"
#include "RenderDescription.h"

#include <vulkan/vulkan.h>

#include "shaders/interop/ShaderResources.h"

#define MAX_MESHES 256

struct gpu_mesh
{
    uint32 FirstVertex;
    uint32 VertexCount;
    uint32 FirstIndex;
    uint32 IndexCount;
};

struct frame_targets
{
    gpu_image Depth;
    gpu_image Scene;
    gpu_image Post;
};

struct render_state
{
    VkCullModeFlags CullMode;
    VkBool32        DepthTest;
    VkBool32        DepthWrite;
    VkBool32        AlphaBlend;
    bool32          Valid;
};

struct render_pipeline
{
    render_state DefaultState;

    VkShaderEXT Vert;
    VkShaderEXT Frag;
};

struct vulkan_pipelines
{
    render_pipeline Render[Pipeline_Count];
};

struct descriptor_heap
{
    VkDescriptorSetLayout Layout;
    gpu_buffer         Buffer;

    VkDeviceSize Offsets[BINDING_COUNT];
};

struct vulkan_resources
{
    descriptor_heap  Heap;
    VkPipelineLayout PipelineLayout;
    VkSampler        Sampler;

    gpu_buffer VertexBuffer;
    gpu_buffer IndexBuffer;
    gpu_buffer FrameBuffers[MAX_FRAMES_IN_FLIGHT];
    gpu_buffer GlobalsBuffer;

    gpu_buffer_slice Globals;

    gpu_mesh       Meshes[MAX_MESHES];
    gpu_image      Textures[MAX_TEXTURES];
    gpu_image      Cubemaps[MAX_CUBEMAPS];
    material       Materials[MAX_MATERIALS];
};

struct vulkan_frame
{
    VkCommandBuffer Cmd;
    uint32          ImageIndex;
    uint32          Slot;
    bool32          Ready;
    bool32          NeedsResize;
};

#endif
