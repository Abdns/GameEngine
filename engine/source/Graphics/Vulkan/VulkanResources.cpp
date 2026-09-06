#include "Vulkan.h"

#define FRAME_BUFFER_SIZE     Megabytes(4)

#define PIPELINE_PUSH_STAGES  (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
#define HEAP_STAGES           (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)

global_variable vulkan_resources GlobalResources;

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

internal void WriteImageDescriptor(vulkan_context *context, descriptor_heap *heap, VkDeviceSize bindingOffset, uint32 arrayElement, VkImageView view, VkDescriptorType type)
{
    Assert(type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    bool32 storage = (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView   = view;

    VkDescriptorGetInfoEXT getInfo{};
    getInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    getInfo.type  = type;

    if (storage)
    {
        getInfo.data.pStorageImage = &imageInfo;
    }
    else
    {
        getInfo.data.pSampledImage = &imageInfo;
    }

    memory_size descriptorSize = storage ? context->DescriptorProps.storageImageDescriptorSize : context->DescriptorProps.sampledImageDescriptorSize;

    uint8 *destination = (uint8 *)heap->Buffer.Mapped + bindingOffset + arrayElement * descriptorSize;

    context->GetDescriptorEXT(context->device, &getInfo, descriptorSize, destination);
}

internal void WriteSamplerDescriptor(vulkan_context *context, descriptor_heap *heap, VkDeviceSize bindingOffset, VkSampler sampler)
{
    VkDescriptorGetInfoEXT getInfo{};
    getInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    getInfo.type          = VK_DESCRIPTOR_TYPE_SAMPLER;
    getInfo.data.pSampler = &sampler;

    memory_size descriptorSize = context->DescriptorProps.samplerDescriptorSize;
    uint8      *destination    = (uint8 *)heap->Buffer.Mapped + bindingOffset;

    context->GetDescriptorEXT(context->device, &getInfo, descriptorSize, destination);
}

internal void CreateDescriptorHeap(vulkan_context *context, vulkan_resources *res)
{
    VkDescriptorSetLayoutBinding bindings[7] = {};
    bindings[0].binding         = BINDING_TEXTURES;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = TEXTURE_HEAP_SIZE;
    bindings[0].stageFlags      = HEAP_STAGES;

    bindings[1].binding         = BINDING_SAMPLER;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = HEAP_STAGES;

    bindings[2].binding         = BINDING_CUBEMAPS;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[2].descriptorCount = MAX_CUBEMAPS;
    bindings[2].stageFlags      = HEAP_STAGES;

    bindings[3].binding         = BINDING_VOLUMES;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[3].descriptorCount = MAX_VOLUMES;
    bindings[3].stageFlags      = HEAP_STAGES;

    bindings[4].binding         = BINDING_STORAGE_VOLUMES;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[4].descriptorCount = MAX_VOLUMES;
    bindings[4].stageFlags      = HEAP_STAGES;

    bindings[5].binding         = BINDING_UINT_VOLUMES;
    bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[5].descriptorCount = MAX_UINT_VOLUMES;
    bindings[5].stageFlags      = HEAP_STAGES;

    bindings[6].binding         = BINDING_VOLUME_SAMPLER;
    bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags      = HEAP_STAGES;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = (uint32)ArrayCount(bindings);
    layoutInfo.pBindings    = bindings;

    VkResult result = vkCreateDescriptorSetLayout(context->device, &layoutInfo, nullptr, &res->Heap.Layout);
    Assert(result == VK_SUCCESS);

    VkDeviceSize heapSize = 0;
    context->GetDescriptorSetLayoutSizeEXT(context->device, res->Heap.Layout, &heapSize);
    heapSize = AlignPow2(heapSize, context->DescriptorProps.descriptorBufferOffsetAlignment);

    context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, res->Heap.Layout, BINDING_TEXTURES, &res->Heap.TextureOffset);
    context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, res->Heap.Layout, BINDING_SAMPLER,  &res->Heap.SamplerOffset);
    context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, res->Heap.Layout, BINDING_CUBEMAPS, &res->Heap.CubemapOffset);
    context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, res->Heap.Layout, BINDING_VOLUMES,  &res->Heap.VolumeOffset);
    context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, res->Heap.Layout, BINDING_STORAGE_VOLUMES, &res->Heap.StorageVolumeOffset);
    context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, res->Heap.Layout, BINDING_UINT_VOLUMES, &res->Heap.UintVolumeOffset);
    context->GetDescriptorSetLayoutBindingOffsetEXT(context->device, res->Heap.Layout, BINDING_VOLUME_SAMPLER, &res->Heap.VolumeSamplerOffset);

    VkBufferUsageFlags heapUsage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

    res->Heap.Buffer = CreateDeviceBuffer(context, heapUsage, heapSize);
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

internal void CreateResources(vulkan_context *context, vulkan_resources *res)
{
    res->FrameArena    = CreateDeviceBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, FRAME_BUFFER_SIZE);
    res->GlobalsBuffer = CreateDeviceBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(frame_globals) * MAX_FRAMES_IN_FLIGHT);
    res->Sampler       = CreateTextureSampler(context, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    res->VolumeSampler = CreateTextureSampler(context, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    CreateDescriptorHeap(context, res);
    res->PipelineLayout = CreatePipelineLayout(context, res->Heap.Layout);

    WriteSamplerDescriptor(context, &res->Heap, res->Heap.SamplerOffset, res->Sampler);
    WriteSamplerDescriptor(context, &res->Heap, res->Heap.VolumeSamplerOffset, res->VolumeSampler);
}

internal void BindDescriptorHeap(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, VkPipelineLayout layout)
{
    VkDescriptorBufferBindingInfoEXT binding{};
    binding.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    binding.address = res->Heap.Buffer.Address;
    binding.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

    context->CmdBindDescriptorBuffersEXT(cmd, 1, &binding);

    uint32       bufferIndex = 0;
    VkDeviceSize setOffset   = 0;

    context->CmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bufferIndex, &setOffset);
    context->CmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,  layout, 0, 1, &bufferIndex, &setOffset);
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

internal gpu_texture *CreateTexture(vulkan_context *context, vulkan_resources *res, uint32 TextureHandle, uint32 Width, uint32 Height, uint32 SRGB, texture_format TextureFormat)
{
    Assert(TextureHandle < MAX_TEXTURES);

    gpu_texture *texture = &res->Textures[TextureHandle];
    Assert(texture->Image == VK_NULL_HANDLE);

    VkFormat format = TextureVkFormat(TextureFormat, SRGB);

    texture->Image = CreateTextureImage(context, Width, Height, format, 1, 1, &texture->Memory);
    texture->View  = CreateColorImageView(context->device, texture->Image, format);

    return texture;
}

internal gpu_texture *CreateCubemap(vulkan_context *context, vulkan_resources *res, uint32 CubemapHandle, uint32 FaceSize, texture_format TextureFormat)
{
    Assert(CubemapHandle < MAX_CUBEMAPS);

    gpu_texture *cube = &res->Cubemaps[CubemapHandle];
    Assert(cube->Image == VK_NULL_HANDLE);

    VkFormat format    = TextureVkFormat(TextureFormat, 0);
    uint32   mipLevels = MipLevelCount(FaceSize);

    cube->MipLevels = mipLevels;
    cube->Image     = CreateTextureImage(context, FaceSize, FaceSize, format, 6, mipLevels, &cube->Memory);
    cube->View      = CreateCubeImageView(context->device, cube->Image, format, mipLevels);

    return cube;
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
