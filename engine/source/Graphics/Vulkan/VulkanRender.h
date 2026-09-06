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

enum buffer_memory
{
    Buffer_GpuOnly = 0,
    Buffer_GpuShared,
    Buffer_Upload,

    Buffer_MemoryCount,
};

struct gpu_buffer
{
    VkBuffer        Buffer;
    VkDeviceMemory  Memory;
    VkDeviceAddress Address;
    void           *Mapped;

    VkDeviceSize Size;
    VkDeviceSize Allocated;
    VkDeviceSize Used;
    VkDeviceSize Limit;

    uint32                MemoryType;
    VkMemoryPropertyFlags MemoryFlags;
};

struct gpu_alloc
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

enum image_kind
{
    Image_Texture = 0,
    Image_Cubemap,
    Image_ColorTarget,
    Image_DepthTarget,
    Image_Volume,

    Image_KindCount,
};

struct gpu_image
{
    VkImage        Image;
    VkImageView    View;
    VkDeviceMemory Memory;

    uint32 Width;
    uint32 Height;
    uint32 Depth;
    uint32 MipLevels;

    VkFormat   Format;
    image_kind Kind;
};

struct descriptor_heap
{
    VkDescriptorSetLayout Layout;
    gpu_buffer         Buffer;

    VkDeviceSize TextureOffset;
    VkDeviceSize SamplerOffset;
    VkDeviceSize CubemapOffset;
    VkDeviceSize VolumeOffset;
    VkDeviceSize StorageVolumeOffset;
    VkDeviceSize UintVolumeOffset;
    VkDeviceSize VolumeSamplerOffset;
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
    VkSampler        VolumeSampler;

    gpu_buffer VertexBuffer;
    gpu_buffer IndexBuffer;
    gpu_buffer MaterialBuffer;
    gpu_buffer FrameArena;
    gpu_buffer GlobalsBuffer;
    gpu_alloc  Globals;

    gpu_mesh       Meshes[MAX_MESHES];
    gpu_image      Textures[MAX_TEXTURES];
    gpu_image      Cubemaps[MAX_CUBEMAPS];
    gpu_image      Volumes[MAX_VOLUMES];
    gpu_image      UintVolumes[MAX_UINT_VOLUMES];
    material_state MaterialStates[MAX_MATERIALS];
    uint32         MaterialCount;
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
