#include "Vulkan.h"

#define FRAME_BUFFER_SIZE     Megabytes(4)

#define PIPELINE_PUSH_STAGES  (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
#define HEAP_STAGES           (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
#define HEAP_BUFFER_USAGE     (VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT)

struct heap_binding_desc
{
    uint32           Binding;
    VkDescriptorType Type;
    uint32           Count;
};

global_variable heap_binding_desc HeapBindingDescs[] =
{
    { BINDING_TEXTURES,        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, TEXTURE_HEAP_SIZE },
    { BINDING_SAMPLER,         VK_DESCRIPTOR_TYPE_SAMPLER,       1                 },
    { BINDING_CUBEMAPS,        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_CUBEMAPS      },
    { BINDING_VOLUMES,         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_VOLUMES       },
    { BINDING_STORAGE_VOLUMES, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_VOLUMES       },
    { BINDING_UINT_VOLUMES,    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_UINT_VOLUMES  },
    { BINDING_VOLUME_SAMPLER,  VK_DESCRIPTOR_TYPE_SAMPLER,       1                 },
};

static_assert(ArrayCount(HeapBindingDescs) == BINDING_COUNT, "HeapBindingDescs must describe every heap binding");

internal VkSampler CreateTextureSampler(vulkan_context *context, VkFilter filter, VkSamplerAddressMode addressMode)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    VkSampler sampler = VK_NULL_HANDLE;

    VkResult result = vkCreateSampler(context->device, &samplerInfo, nullptr, &sampler);
    Assert(result == VK_SUCCESS);

    return sampler;
}

internal memory_size HeapDescriptorSize(vulkan_context *context, VkDescriptorType type)
{
    if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
    {
        return context->DescriptorProps.storageImageDescriptorSize;
    }

    if (type == VK_DESCRIPTOR_TYPE_SAMPLER)
    {
        return context->DescriptorProps.samplerDescriptorSize;
    }

    return context->DescriptorProps.sampledImageDescriptorSize;
}

internal uint8 *HeapSlotAddress(vulkan_context *context, descriptor_heap *heap, uint32 binding, uint32 arrayElement)
{
    Assert(binding < BINDING_COUNT);
    Assert(arrayElement < HeapBindingDescs[binding].Count);

    memory_size descriptorSize = HeapDescriptorSize(context, HeapBindingDescs[binding].Type);

    return (uint8 *)heap->Buffer.Mapped + heap->Offsets[binding] + arrayElement * descriptorSize;
}

internal void WriteHeapImage(vulkan_context *context, descriptor_heap *heap, uint32 binding, uint32 arrayElement, VkImageView view)
{
    VkDescriptorType type = HeapBindingDescs[binding].Type;

    Assert(type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView   = view;

    VkDescriptorGetInfoEXT getInfo{};
    getInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    getInfo.type  = type;

    if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
    {
        getInfo.data.pStorageImage = &imageInfo;
    }
    else
    {
        getInfo.data.pSampledImage = &imageInfo;
    }

    uint8 *destination = HeapSlotAddress(context, heap, binding, arrayElement);

    context->GetDescriptorEXT(context->device, &getInfo, HeapDescriptorSize(context, type), destination);
}

internal void WriteHeapSampler(vulkan_context *context, descriptor_heap *heap, uint32 binding, uint32 arrayElement, VkSampler sampler)
{
    Assert(HeapBindingDescs[binding].Type == VK_DESCRIPTOR_TYPE_SAMPLER);

    VkDescriptorGetInfoEXT getInfo{};
    getInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    getInfo.type          = VK_DESCRIPTOR_TYPE_SAMPLER;
    getInfo.data.pSampler = &sampler;

    uint8 *destination = HeapSlotAddress(context, heap, binding, arrayElement);

    context->GetDescriptorEXT(context->device, &getInfo, context->DescriptorProps.samplerDescriptorSize, destination);
}

internal descriptor_heap CreateDescriptorHeap(vulkan_context *context)
{
    descriptor_heap heap = {};

    VkDescriptorSetLayoutBinding bindings[BINDING_COUNT] = {};

    for (uint32 i = 0; i < BINDING_COUNT; ++i)
    {
        Assert(HeapBindingDescs[i].Binding == i);

        bindings[i].binding         = HeapBindingDescs[i].Binding;
        bindings[i].descriptorType  = HeapBindingDescs[i].Type;
        bindings[i].descriptorCount = HeapBindingDescs[i].Count;
        bindings[i].stageFlags      = HEAP_STAGES;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = (uint32)ArrayCount(bindings);
    layoutInfo.pBindings    = bindings;

    VkResult result = vkCreateDescriptorSetLayout(context->device, &layoutInfo, nullptr, &heap.Layout);
    Assert(result == VK_SUCCESS);

    VkDeviceSize heapSize = 0;
    context->GetDescriptorSetLayoutSizeEXT(context->device, heap.Layout, &heapSize);
    heapSize = AlignPow2(heapSize, context->DescriptorProps.descriptorBufferOffsetAlignment);

    for (uint32 i = 0; i < BINDING_COUNT; ++i)
    {
        context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, heap.Layout, HeapBindingDescs[i].Binding, &heap.Offsets[i]);
    }

    heap.Buffer = CreateBuffer(context, Buffer_GpuShared, HEAP_BUFFER_USAGE, heapSize);

    return heap;
}

internal VkPushConstantRange ParamsPushRange()
{
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = PIPELINE_PUSH_STAGES;
    pushRange.offset = 0;
    pushRange.size = (uint32)sizeof(push_constants);

    return pushRange;
}

internal VkPipelineLayout CreatePipelineLayout(vulkan_context *context, VkDescriptorSetLayout heapLayout)
{
    VkPushConstantRange pushRange = ParamsPushRange();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &heapLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;

    VkResult result = vkCreatePipelineLayout(context->device, &layoutInfo, nullptr, &layout);
    Assert(result == VK_SUCCESS);

    return layout;
}

internal vulkan_resources CreateResources(vulkan_context *context)
{
    vulkan_resources res = {};

    res.FrameArena    = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, FRAME_BUFFER_SIZE);
    res.GlobalsBuffer = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(frame_globals) * MAX_FRAMES_IN_FLIGHT);
    res.Sampler       = CreateTextureSampler(context, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    res.VolumeSampler = CreateTextureSampler(context, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    res.Heap = CreateDescriptorHeap(context);
    res.PipelineLayout = CreatePipelineLayout(context, res.Heap.Layout);

    WriteHeapSampler(context, &res.Heap, BINDING_SAMPLER,        0, res.Sampler);
    WriteHeapSampler(context, &res.Heap, BINDING_VOLUME_SAMPLER, 0, res.VolumeSampler);

    return res;
}

internal void BindDescriptorHeap(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, VkPipelineLayout layout)
{
    VkDescriptorBufferBindingInfoEXT binding{};
    binding.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    binding.address = res->Heap.Buffer.Address;
    binding.usage = HEAP_BUFFER_USAGE;

    context->CmdBindDescriptorBuffersEXT(cmd, 1, &binding);

    uint32       bufferIndex = 0;
    VkDeviceSize setOffset   = 0;

    context->CmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, SET_GLOBAL, 1, &bufferIndex, &setOffset);
    context->CmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,  layout, SET_GLOBAL, 1, &bufferIndex, &setOffset);
}

internal gpu_mesh CreateMesh(VkDeviceSize vertexOffset, uint32 VertexCount, VkDeviceSize indexOffset, uint32 IndexCount)
{
    Assert((vertexOffset % sizeof(vertex)) == 0);

    gpu_mesh result;
    result.FirstVertex = (uint32)(vertexOffset / sizeof(vertex));
    result.VertexCount = VertexCount;
    result.FirstIndex  = (uint32)(indexOffset / sizeof(uint32));
    result.IndexCount  = IndexCount;

    return result;
}

internal VkFormat TextureVkFormat(texture_format Format, uint32 SRGB)
{
    if (Format == TextureFormat_RGBA16F)
    {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }

    return SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
}

internal gpu_image *CreateTexture(vulkan_context *context, vulkan_resources *res, uint32 TextureHandle, uint32 Width, uint32 Height, uint32 SRGB, texture_format TextureFormat)
{
    Assert(TextureHandle < MAX_TEXTURES);

    gpu_image *texture = &res->Textures[TextureHandle];
    Assert(texture->Image == VK_NULL_HANDLE);

    VkFormat format = TextureVkFormat(TextureFormat, SRGB);

    *texture = CreateImage(context, Image_Texture, format, Width, Height, 1, 1);

    return texture;
}

internal gpu_image *CreateCubemap(vulkan_context *context, vulkan_resources *res, uint32 CubemapHandle, uint32 FaceSize, texture_format TextureFormat)
{
    Assert(CubemapHandle < MAX_CUBEMAPS);

    gpu_image *cube = &res->Cubemaps[CubemapHandle];
    Assert(cube->Image == VK_NULL_HANDLE);

    VkFormat format    = TextureVkFormat(TextureFormat, 0);
    uint32   mipLevels = MipLevelCount(FaceSize);

    *cube = CreateImage(context, Image_Cubemap, format, FaceSize, FaceSize, 1, mipLevels);

    return cube;
}

internal gpu_image *CreateVolume(vulkan_context *context, vulkan_resources *res, uint32 volumeSlot, uint32 width, uint32 height, uint32 depth)
{
    Assert(volumeSlot < MAX_VOLUMES);

    gpu_image *volume = &res->Volumes[volumeSlot];
    Assert(volume->Image == VK_NULL_HANDLE);

    *volume = CreateImage(context, Image_Volume, VK_FORMAT_R16G16B16A16_SFLOAT, width, height, depth, 1);

    return volume;
}

internal gpu_image *CreateUintVolume(vulkan_context *context, vulkan_resources *res, uint32 volumeSlot, uint32 width, uint32 height, uint32 depth)
{
    Assert(volumeSlot < MAX_UINT_VOLUMES);

    gpu_image *volume = &res->UintVolumes[volumeSlot];
    Assert(volume->Image == VK_NULL_HANDLE);

    *volume = CreateImage(context, Image_Volume, VK_FORMAT_R32_UINT, width, height, depth, 1);

    return volume;
}

internal material_state CreateMaterialState(command_load_material *Description)
{
    Assert(Description->Pipeline < Pipeline_MeshCount);

    material_state result;
    result.Pipeline   = Description->Pipeline;
    result.CullMode   = Description->CullMode;
    result.BlendMode  = Description->BlendMode;
    result.Queue      = Description->Queue;
    result.DepthTest  = Description->DepthTest;
    result.DepthWrite = Description->DepthWrite;

    return result;
}

internal gpu_material CreateMaterial(command_load_material *Description)
{
    gpu_material result = {};
    result.BaseColor   = Description->BaseColor;
    result.TextureSlot = Description->TextureHandle;
    result.Metallic    = Description->Metallic;
    result.Roughness   = Description->Roughness;

    return result;
}
