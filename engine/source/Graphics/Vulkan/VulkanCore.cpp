#include "Vulkan.h"

#define MEMORY_DEVICE VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
#define MEMORY_HOST   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)

#define IMAGE_TRANSFER (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)

struct buffer_memory_desc
{
    VkMemoryPropertyFlags Wanted;
    VkMemoryPropertyFlags Accepted;

    bool32 Mapped;
};

global_variable buffer_memory_desc BufferMemoryDescs[] =
{
    { MEMORY_DEVICE,               MEMORY_DEVICE, false },
    { MEMORY_DEVICE | MEMORY_HOST, MEMORY_HOST,   true  },
    { MEMORY_HOST,                 MEMORY_HOST,   true  },
};

static_assert(ArrayCount(BufferMemoryDescs) == Buffer_MemoryCount, "BufferMemoryDescs must describe every buffer_memory");

struct image_kind_desc
{
    VkImageType        Type;
    VkImageViewType    ViewType;
    VkImageCreateFlags Flags;
    VkImageUsageFlags  Usage;
    VkImageAspectFlags Aspect;

    uint32 Layers;
};

global_variable image_kind_desc ImageKindDescs[] =
{
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D,   0,                                       IMAGE_TRANSFER | VK_IMAGE_USAGE_SAMPLED_BIT,                              VK_IMAGE_ASPECT_COLOR_BIT, 1 },
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,     IMAGE_TRANSFER | VK_IMAGE_USAGE_SAMPLED_BIT,                              VK_IMAGE_ASPECT_COLOR_BIT, 6 },
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D,   0,                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,         VK_IMAGE_ASPECT_COLOR_BIT, 1 },
    { VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D,   0,                                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1 },
    { VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D,   0,                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,                  VK_IMAGE_ASPECT_COLOR_BIT, 1 },
};

static_assert(ArrayCount(ImageKindDescs) == Image_KindCount, "ImageKindDescs must describe every image_kind");

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

internal bool32 AllocateMemory(vulkan_context *context, VkMemoryRequirements *memReq, VkMemoryPropertyFlags properties, bool32 deviceAddress, VkDeviceMemory *outMemory, uint32 *outMemoryType)
{
    VkPhysicalDeviceMemoryProperties *memProps = &context->MemoryProps;

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    for (uint32 i = 0; i < memProps->memoryTypeCount; ++i)
    {
        if ((memReq->memoryTypeBits & (1u << i)) == 0)
        {
            continue;
        }

        if ((memProps->memoryTypes[i].propertyFlags & properties) != properties)
        {
            continue;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq->size;
        allocInfo.memoryTypeIndex = i;
        allocInfo.pNext           = deviceAddress ? &flagsInfo : nullptr;

        if (vkAllocateMemory(context->device, &allocInfo, nullptr, outMemory) == VK_SUCCESS)
        {
            *outMemoryType = i;
            return true;
        }
    }

    return false;
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

internal gpu_buffer CreateBuffer(vulkan_context *context, buffer_memory kind, VkBufferUsageFlags usage, VkDeviceSize size)
{
    Assert(kind < Buffer_MemoryCount);

    buffer_memory_desc *desc = &BufferMemoryDescs[kind];

    gpu_buffer buffer = {};
    buffer.Buffer = CreateBufferObject(context, usage, size);
    buffer.Size   = size;
    buffer.Limit  = size;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(context->device, buffer.Buffer, &memReq);

    uint32 memoryType = 0;

    bool32 allocated = AllocateMemory(context, &memReq, desc->Wanted, true, &buffer.Memory, &memoryType);

    if (!allocated && desc->Accepted != desc->Wanted)
    {
        allocated = AllocateMemory(context, &memReq, desc->Accepted, true, &buffer.Memory, &memoryType);
    }

    Assert(allocated);

    VkResult bound = vkBindBufferMemory(context->device, buffer.Buffer, buffer.Memory, 0);
    Assert(bound == VK_SUCCESS);

    buffer.Allocated   = memReq.size;
    buffer.MemoryType  = memoryType;
    buffer.MemoryFlags = context->MemoryProps.memoryTypes[memoryType].propertyFlags;
    buffer.Address     = BufferAddress(context, buffer.Buffer);

    if (desc->Mapped)
    {
        Assert((buffer.MemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);

        VkResult mapped = vkMapMemory(context->device, buffer.Memory, 0, size, 0, &buffer.Mapped);
        Assert(mapped == VK_SUCCESS);
    }

    DebugLog("Buffer %llu bytes: memory type %u, device-local %u, host-visible %u\n", (uint64)size, memoryType, (buffer.MemoryFlags & MEMORY_DEVICE) != 0, (buffer.MemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);

    return buffer;
}

internal void DestroyBuffer(vulkan_context *context, gpu_buffer *buffer)
{
    if (buffer->Mapped)
    {
        vkUnmapMemory(context->device, buffer->Memory);
    }

    vkDestroyBuffer(context->device, buffer->Buffer, nullptr);
    vkFreeMemory(context->device, buffer->Memory, nullptr);

    *buffer = {};
}

internal gpu_alloc BufferAlloc(gpu_buffer *buffer, VkDeviceSize size, VkDeviceSize alignment)
{
    VkDeviceSize offset = AlignPow2(buffer->Used, alignment);

    Assert(offset + size <= buffer->Limit);

    gpu_alloc result;
    result.Cpu    = buffer->Mapped ? (uint8 *)buffer->Mapped + offset : nullptr;
    result.Gpu    = buffer->Address + offset;
    result.Offset = offset;

    buffer->Used = offset + size;

    return result;
}

internal gpu_alloc BufferWrite(gpu_buffer *buffer, const void *data, VkDeviceSize size, VkDeviceSize alignment)
{
    gpu_alloc result = BufferAlloc(buffer, size, alignment);

    Assert(result.Cpu);

    CopySize(size, (void *)data, result.Cpu);

    return result;
}

internal gpu_alloc BufferSlot(gpu_buffer *buffer, uint32 slot, VkDeviceSize size)
{
    VkDeviceSize offset = (VkDeviceSize)slot * size;

    Assert(offset + size <= buffer->Size);

    gpu_alloc result;
    result.Cpu    = buffer->Mapped ? (uint8 *)buffer->Mapped + offset : nullptr;
    result.Gpu    = buffer->Address + offset;
    result.Offset = offset;

    return result;
}

internal void ResetFrameRegion(gpu_buffer *buffer, uint32 frameSlot)
{
    VkDeviceSize regionSize = buffer->Size / MAX_FRAMES_IN_FLIGHT;

    buffer->Used  = frameSlot * regionSize;
    buffer->Limit = buffer->Used + regionSize;
}

internal VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageViewType viewType, VkImageAspectFlags aspect, uint32 layers, uint32 mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = image;
    viewInfo.viewType = viewType;
    viewInfo.format   = format;

    viewInfo.subresourceRange.aspectMask     = aspect;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = layers;

    VkImageView view = VK_NULL_HANDLE;

    VkResult created = vkCreateImageView(device, &viewInfo, nullptr, &view);
    Assert(created == VK_SUCCESS);

    return view;
}

internal gpu_image CreateImage(vulkan_context *context, image_kind kind, VkFormat format, uint32 width, uint32 height, uint32 depth, uint32 mipLevels)
{
    Assert(kind < Image_KindCount);

    image_kind_desc *desc = &ImageKindDescs[kind];

    gpu_image image = {};
    image.Width     = width;
    image.Height    = height;
    image.Depth     = depth;
    image.MipLevels = mipLevels;
    image.Format    = format;
    image.Kind      = kind;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags         = desc->Flags;
    imageInfo.imageType     = desc->Type;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = depth;
    imageInfo.mipLevels     = mipLevels;
    imageInfo.arrayLayers   = desc->Layers;
    imageInfo.format        = format;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = desc->Usage;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VkResult created = vkCreateImage(context->device, &imageInfo, nullptr, &image.Image);
    Assert(created == VK_SUCCESS);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(context->device, image.Image, &memReq);

    uint32 memoryType = 0;

    bool32 allocated = AllocateMemory(context, &memReq, MEMORY_DEVICE, false, &image.Memory, &memoryType);
    Assert(allocated);

    VkResult bound = vkBindImageMemory(context->device, image.Image, image.Memory, 0);
    Assert(bound == VK_SUCCESS);

    image.View = CreateImageView(context->device, image.Image, format, desc->ViewType, desc->Aspect, desc->Layers, mipLevels);

    return image;
}

internal void DestroyImage(vulkan_context *context, gpu_image *image)
{
    vkDestroyImageView(context->device, image->View, nullptr);
    vkDestroyImage(context->device, image->Image, nullptr);
    vkFreeMemory(context->device, image->Memory, nullptr);

    *image = {};
}

internal VkImageSubresourceRange ImageRange(VkImageAspectFlags aspect, uint32 baseMipLevel, uint32 levelCount, uint32 layers)
{
    VkImageSubresourceRange range{};
    range.aspectMask     = aspect;
    range.baseMipLevel   = baseMipLevel;
    range.levelCount     = levelCount;
    range.baseArrayLayer = 0;
    range.layerCount     = layers;

    return range;
}

internal void CmdImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange range, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask        = srcStage;
    barrier.srcAccessMask       = srcAccess;
    barrier.dstStageMask        = dstStage;
    barrier.dstAccessMask       = dstAccess;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = range;

    VkDependencyInfo dependency{};
    dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependency);
}

internal void CmdImageToGeneral(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32 layers, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageSubresourceRange range = ImageRange(aspect, 0, VK_REMAINING_MIP_LEVELS, layers);

    CmdImageBarrier(cmd, image, range, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0, dstStage, dstAccess);
}

internal void CmdImageToPresent(VkCommandBuffer cmd, VkImage image)
{
    VkImageSubresourceRange range = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1);

    CmdImageBarrier(cmd, image, range, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
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
    VkImageSubresourceRange range = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, layers);

    CmdImageToGeneral(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, layers, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CmdCopyBufferToImage(cmd, staging, stagingOffset, image, width, height, layers);

    CmdImageBarrier(cmd, image, range, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
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

internal void CmdGenerateMips(VkCommandBuffer cmd, VkImage image, uint32 width, uint32 height, uint32 layers, uint32 mipLevels)
{
    for (uint32 level = 1; level < mipLevels; ++level)
    {
        VkImageSubresourceRange source = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, layers);

        CmdImageBarrier(cmd, image, source, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

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

    VkImageSubresourceRange all = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, layers);

    CmdImageBarrier(cmd, image, all, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
