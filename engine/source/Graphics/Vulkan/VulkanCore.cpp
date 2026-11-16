#include "Vulkan.h"

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

internal void CmdAcquiredImageToGeneral(VkCommandBuffer cmd, VkImage image)
{
    VkImageSubresourceRange range = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1);

    CmdImageBarrier(cmd, image, range, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

internal void CmdClearImage(VkCommandBuffer cmd, VkImage image)
{
    VkClearColorValue clear{};

    VkImageSubresourceRange range = ImageRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 1);

    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
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

    CmdImageToGeneral(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, layers, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
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
        srcWidth = srcWidth > 0 ? srcWidth : 1;
        srcHeight = srcHeight > 0 ? srcHeight : 1;
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
