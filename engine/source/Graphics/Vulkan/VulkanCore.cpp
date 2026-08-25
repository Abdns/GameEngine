#include "Vulkan.h"

#define INVALID_MEMORY_TYPE 0xFFFFFFFF

internal uint32 FindMemoryType(vulkan_context *context, uint32 typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties *memProps = &context->MemoryProps;

    for (uint32 i = 0; i < memProps->memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) && (memProps->memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    return INVALID_MEMORY_TYPE;
}

internal VkCommandBuffer BeginSingleTimeCommands(vulkan_context *context)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = context->commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(context->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    return cmd;
}

internal void EndSingleTimeCommands(vulkan_context *context, VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(context->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context->graphicsQueue);

    vkFreeCommandBuffers(context->device, context->commandPool, 1, &cmd);
}

internal VkDeviceMemory AllocateBufferMemory(vulkan_context *context, VkBuffer buffer, VkMemoryPropertyFlags preferred, VkMemoryPropertyFlags required, uint32 *outMemoryType)
{
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(context->device, buffer, &memReq);

    uint32 memoryType = FindMemoryType(context, memReq.memoryTypeBits, preferred);
    if (memoryType == INVALID_MEMORY_TYPE && preferred != required)
    {
        memoryType = FindMemoryType(context, memReq.memoryTypeBits, required);
    }

    Assert(memoryType != INVALID_MEMORY_TYPE);

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;
    allocInfo.pNext           = &flagsInfo;

    VkDeviceMemory memory = VK_NULL_HANDLE;

    VkResult allocated = vkAllocateMemory(context->device, &allocInfo, nullptr, &memory);
    Assert(allocated == VK_SUCCESS);

    VkResult bound = vkBindBufferMemory(context->device, buffer, memory, 0);
    Assert(bound == VK_SUCCESS);

    *outMemoryType = memoryType;
    return memory;
}

internal VkBuffer CreateBufferObject(vulkan_context *context, VkBufferUsageFlags usage, VkDeviceSize size)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;

    VkResult created = vkCreateBuffer(context->device, &bufferInfo, nullptr, &buffer);
    Assert(created == VK_SUCCESS);

    return buffer;
}

internal VkDeviceAddress BufferAddress(vulkan_context *context, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;

    return vkGetBufferDeviceAddress(context->device, &addressInfo);
}

internal gpu_buffer CreateGpuBuffer(vulkan_context *context, VkBufferUsageFlags usage, VkDeviceSize size)
{
    gpu_buffer buffer = {};

    uint32 memoryType = 0;

    buffer.Buffer  = CreateBufferObject(context, usage, size);
    buffer.Memory  = AllocateBufferMemory(context, buffer.Buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memoryType);
    buffer.Address = BufferAddress(context, buffer.Buffer);
    buffer.Size    = size;
    buffer.Limit   = size;

    return buffer;
}

internal void DestroyGpuBuffer(vulkan_context *context, gpu_buffer *buffer)
{
    vkDestroyBuffer(context->device, buffer->Buffer, nullptr);
    vkFreeMemory(context->device, buffer->Memory, nullptr);

    *buffer = {};
}

internal gpu_alloc GpuBufferAlloc(gpu_buffer *buffer, VkDeviceSize size, VkDeviceSize alignment)
{
    VkDeviceSize offset = AlignPow2(buffer->Used, alignment);

    Assert(offset + size <= buffer->Limit);

    gpu_alloc result;
    result.Gpu    = buffer->Address + offset;
    result.Offset = offset;

    buffer->Used = offset + size;

    return result;
}

internal shared_buffer CreateSharedBuffer(vulkan_context *context, VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags preferred)
{
    VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    shared_buffer buffer = {};

    uint32 memoryType = 0;

    buffer.Buffer  = CreateBufferObject(context, usage, size);
    buffer.Memory  = AllocateBufferMemory(context, buffer.Buffer, preferred | required, required, &memoryType);
    buffer.Address = BufferAddress(context, buffer.Buffer);
    buffer.Size    = size;
    buffer.Limit   = size;

    VkResult mapped = vkMapMemory(context->device, buffer.Memory, 0, size, 0, &buffer.Mapped);
    Assert(mapped == VK_SUCCESS);

    return buffer;
}

internal shared_buffer CreateDeviceBuffer(vulkan_context *context, VkBufferUsageFlags usage, VkDeviceSize size)
{
    return CreateSharedBuffer(context, usage, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

internal shared_buffer CreateUploadBuffer(vulkan_context *context, VkBufferUsageFlags usage, VkDeviceSize size)
{
    return CreateSharedBuffer(context, usage, size, 0);
}

internal void DestroySharedBuffer(vulkan_context *context, shared_buffer *buffer)
{
    vkUnmapMemory(context->device, buffer->Memory);

    vkDestroyBuffer(context->device, buffer->Buffer, nullptr);
    vkFreeMemory(context->device, buffer->Memory, nullptr);

    *buffer = {};
}

internal shared_alloc SharedBufferAlloc(shared_buffer *buffer, VkDeviceSize size, VkDeviceSize alignment)
{
    VkDeviceSize offset = AlignPow2(buffer->Used, alignment);

    Assert(offset + size <= buffer->Limit);

    shared_alloc result;
    result.Cpu    = (uint8 *)buffer->Mapped + offset;
    result.Gpu    = buffer->Address + offset;
    result.Offset = offset;

    buffer->Used = offset + size;

    return result;
}

internal shared_alloc SharedBufferWrite(shared_buffer *buffer, const void *data, VkDeviceSize size, VkDeviceSize alignment)
{
    shared_alloc result = SharedBufferAlloc(buffer, size, alignment);

    CopySize(size, (void *)data, result.Cpu);

    return result;
}

internal shared_alloc GetSharedBufferSlot(shared_buffer *buffer, uint32 slot, VkDeviceSize size)
{
    VkDeviceSize offset = (VkDeviceSize)slot * size;

    Assert(offset + size <= buffer->Size);

    shared_alloc result;
    result.Cpu    = (uint8 *)buffer->Mapped + offset;
    result.Gpu    = buffer->Address + offset;
    result.Offset = offset;

    return result;
}

internal void ResetFrameRegion(shared_buffer *buffer, uint32 frameSlot)
{
    VkDeviceSize regionSize = buffer->Size / MAX_FRAMES_IN_FLIGHT;

    buffer->Used  = frameSlot * regionSize;
    buffer->Limit = buffer->Used + regionSize;
}

internal VkImageCreateInfo ImageInfo(uint32 width, uint32 height, VkFormat format, uint32 layers, uint32 mipLevels, VkImageTiling tiling, VkImageUsageFlags usage)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = layers;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (layers == 6)
    {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    return imageInfo;
}

internal VkImage CreateImage(vulkan_context *context, uint32 width, uint32 height, VkFormat format, uint32 layers, uint32 mipLevels, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryProperties, VkDeviceMemory *outMemory)
{
    VkImageCreateInfo imageInfo = ImageInfo(width, height, format, layers, mipLevels, tiling, usage);

    VkImage image = VK_NULL_HANDLE;

    VkResult created = vkCreateImage(context->device, &imageInfo, nullptr, &image);
    Assert(created == VK_SUCCESS);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(context->device, image, &memReq);

    uint32 memoryType = FindMemoryType(context, memReq.memoryTypeBits, memoryProperties);
    Assert(memoryType != INVALID_MEMORY_TYPE);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;

    VkResult allocated = vkAllocateMemory(context->device, &allocInfo, nullptr, outMemory);
    Assert(allocated == VK_SUCCESS);

    VkResult bound = vkBindImageMemory(context->device, image, *outMemory, 0);
    Assert(bound == VK_SUCCESS);

    return image;
}

internal VkImage CreateTextureImage(vulkan_context *context, uint32 width, uint32 height, VkFormat format, uint32 layers, uint32 mipLevels, VkDeviceMemory *outMemory)
{
    return CreateImage(context, width, height, format, layers, mipLevels, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outMemory);
}

internal VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectMask, VkImageViewType viewType, uint32 layers, uint32 mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;

    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layers;

    VkImageView view = VK_NULL_HANDLE;

    VkResult created = vkCreateImageView(device, &viewInfo, nullptr, &view);
    Assert(created == VK_SUCCESS);

    return view;
}

internal VkImageView CreateColorImageView(VkDevice device, VkImage image, VkFormat format)
{
    return CreateImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1);
}

internal VkImageView CreateCubeImageView(VkDevice device, VkImage image, VkFormat format, uint32 mipLevels)
{
    return CreateImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6, mipLevels);
}

internal VkImageView CreateDepthImageView(VkDevice device, VkImage image, VkFormat format)
{
    return CreateImageView(device, image, format, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1);
}

internal void DestroyTexture(vulkan_context *context, gpu_texture *texture)
{
    vkDestroyImageView(context->device, texture->View, nullptr);
    vkDestroyImage(context->device, texture->Image, nullptr);
    vkFreeMemory(context->device, texture->Memory, nullptr);

    *texture = {};
}

internal void CmdImageToGeneral(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32 layers, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask                = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask               = 0;
    barrier.dstStageMask                = dstStage;
    barrier.dstAccessMask               = dstAccess;
    barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                   = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                       = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.layerCount = layers;

    VkDependencyInfo dependency{};
    dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependency);
}

internal void CmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize bufferOffset, VkImage image, uint32 width, uint32 height, uint32 layers)
{
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layers;
    region.imageExtent.width  = width;
    region.imageExtent.height = height;
    region.imageExtent.depth  = 1;

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
}

internal void CmdUploadImage(VkCommandBuffer cmd, VkBuffer staging, VkDeviceSize stagingOffset, VkImage image, uint32 width, uint32 height, uint32 layers)
{
    CmdImageToGeneral(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, layers, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CmdCopyBufferToImage(cmd, staging, stagingOffset, image, width, height, layers);
}

internal uint32 MipLevelCount(uint32 size)
{
    uint32 levels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++levels;
    }

    return levels;
}

internal void CmdMipBarrier(VkCommandBuffer cmd, VkImage image, uint32 baseMipLevel, uint32 levelCount, uint32 layers, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType                         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask                  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask                 = srcAccess;
    barrier.dstStageMask                  = dstStage;
    barrier.dstAccessMask                 = dstAccess;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout                     = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                         = image;
    barrier.subresourceRange.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount   = levelCount;
    barrier.subresourceRange.layerCount   = layers;

    VkDependencyInfo dependency{};
    dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependency);
}

internal void CmdGenerateMips(VkCommandBuffer cmd, VkImage image, uint32 width, uint32 height, uint32 layers, uint32 mipLevels)
{
    for (uint32 level = 1; level < mipLevels; ++level)
    {
        CmdMipBarrier(cmd, image, level - 1, 1, layers, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        uint32 srcWidth  = width  >> (level - 1);
        uint32 srcHeight = height >> (level - 1);
        uint32 dstWidth  = srcWidth  > 1 ? (srcWidth  >> 1) : 1;
        uint32 dstHeight = srcHeight > 1 ? (srcHeight >> 1) : 1;

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel       = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount     = layers;
        blit.srcOffsets[1].x               = (int32)srcWidth;
        blit.srcOffsets[1].y               = (int32)srcHeight;
        blit.srcOffsets[1].z               = 1;
        blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel       = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount     = layers;
        blit.dstOffsets[1].x               = (int32)dstWidth;
        blit.dstOffsets[1].y               = (int32)dstHeight;
        blit.dstOffsets[1].z               = 1;

        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
    }

    CmdMipBarrier(cmd, image, 0, VK_REMAINING_MIP_LEVELS, layers, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

internal void CmdImageToPresent(VkCommandBuffer cmd, VkImage image)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask                = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask               = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask                = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask               = 0;
    barrier.oldLayout                   = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout                   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                       = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependency{};
    dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependency);
}
