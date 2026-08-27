#ifndef VULKANRENDER_H
#define VULKANRENDER_H

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"

#include <vulkan/vulkan.h>

#include "shaders/ShaderInterop.h"

#define MAX_SWAPCHAIN_IMAGES  8
#define MAX_FRAMES_IN_FLIGHT  2
#define MAX_MESHES            256
#define FRAME_BUFFER_SIZE     Megabytes(4)
#define STAGING_MEMORY_SIZE   Megabytes(64)

#define PIPELINE_TOPOLOGY     VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
#define PIPELINE_FRONT_FACE   VK_FRONT_FACE_CLOCKWISE
#define PIPELINE_PUSH_STAGES  (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
#define HEAP_STAGES           (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)

struct vulkan_shader
{
    file_data vert;
    file_data frag;
};

struct gpu_buffer
{
    VkBuffer        Buffer;
    VkDeviceMemory  Memory;
    VkDeviceSize    Size;
    VkDeviceSize    Used;
    VkDeviceSize    Limit;

    VkDeviceAddress Address;
};

struct shared_buffer
{
    VkBuffer        Buffer;
    VkDeviceMemory  Memory;
    VkDeviceSize    Size;
    VkDeviceSize    Used;
    VkDeviceSize    Limit;

    void           *Mapped;
    VkDeviceAddress Address;
};

struct gpu_alloc
{
    VkDeviceAddress Gpu;
    VkDeviceSize    Offset;
};

struct shared_alloc
{
    void           *Cpu;
    VkDeviceAddress Gpu;
    VkDeviceSize    Offset;
};


struct gpu_mesh
{
    uint32 FirstVertex;
    uint32 VertexCount;
    uint32 FirstIndex;
    uint32 IndexCount;
};

struct gpu_texture
{
    VkImage        Image;
    VkImageView    View;
    uint32         MipLevels;
    VkDeviceMemory Memory;
};

struct gpu_volume
{
    VkImage        Image;
    VkImageView    View;
    VkDeviceMemory Memory;
    uint32         Width;
    uint32         Height;
    uint32         Depth;
};


struct descriptor_heap
{
    VkDescriptorSetLayout Layout;
    shared_buffer         Buffer;

    VkDeviceSize TextureOffset;
    VkDeviceSize SamplerOffset;
    VkDeviceSize CubemapOffset;
    VkDeviceSize VolumeOffset;
    VkDeviceSize StorageVolumeOffset;
};

struct material_state
{
    pipeline_type Pipeline;
    cull_mode     CullMode;
    blend_mode    BlendMode;
    render_queue  Queue;
    bool32        DepthTest;
    bool32        DepthWrite;
};

struct vulkan_resources
{
    descriptor_heap  Heap;
    VkPipelineLayout PipelineLayout;
    VkSampler        Sampler;

    shared_buffer VertexBuffer;
    shared_buffer IndexBuffer;
    shared_buffer MaterialBuffer;
    shared_buffer FrameArena;
    shared_buffer GlobalsBuffer;
    shared_alloc  Globals;

    gpu_mesh       Meshes[MAX_MESHES];
    gpu_texture    Textures[MAX_TEXTURES];
    gpu_texture    Cubemaps[MAX_CUBEMAPS];
    gpu_volume     Volumes[MAX_VOLUMES];
    material_state MaterialStates[MAX_MATERIALS];
    uint32         MaterialCount;
};

struct render_state
{
    VkCullModeFlags CullMode;
    VkBool32        DepthTest;
    VkBool32        DepthWrite;
    VkBool32        AlphaBlend;
    bool32          Valid;
};

struct pipeline_desc
{
    const char *ShaderName;

    VkBool32 DepthTest;
    VkBool32 DepthWrite;
    VkBool32 Blend;
};

struct render_pipeline
{
    render_state DefaultState;

    VkShaderEXT Vert;
    VkShaderEXT Frag;
};

enum compute_type
{
    Compute_VoxelClear = 0,
    Compute_Voxelize,

    Compute_Count,
};

struct compute_pipeline
{
    VkShaderEXT Compute;
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
