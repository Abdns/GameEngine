#include "Vulkan.h"

internal gpu_texture CreateRenderTarget(vulkan_context *context, vulkan_resources *res, uint32 textureSlot, VkFormat format)
{
    gpu_texture target = {};

    target.Image = CreateImage(
        context,
        context->swapchainExtent.width,
        context->swapchainExtent.height,
        format, 1, 1,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &target.Memory);

    target.View = CreateColorImageView(context->device, target.Image, format);

    VkCommandBuffer cmd = BeginSingleTimeCommands(context);
    CmdImageToGeneral(cmd, target.Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    EndSingleTimeCommands(context, cmd);

    WriteImageDescriptor(context, &res->Heap, res->Heap.TextureOffset, textureSlot, target.View);

    return target;
}

internal void BeginPass(vulkan_context *context, VkCommandBuffer cmd, VkImageView target, VkAttachmentLoadOp colorLoad, Vector4 clearColor, bool32 useDepth)
{
    VkRenderingAttachmentInfo color{};
    color.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView   = target;
    color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    color.loadOp      = colorLoad;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    color.clearValue.color.float32[0] = clearColor.X;
    color.clearValue.color.float32[1] = clearColor.Y;
    color.clearValue.color.float32[2] = clearColor.Z;
    color.clearValue.color.float32[3] = clearColor.W;

    VkRenderingAttachmentInfo depth{};
    depth.sType                        = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView                    = context->depth.View;
    depth.imageLayout                  = VK_IMAGE_LAYOUT_GENERAL;
    depth.loadOp                       = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp                      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo rendering{};
    rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent    = context->swapchainExtent;
    rendering.layerCount           = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments    = &color;
    rendering.pDepthAttachment     = useDepth ? &depth : nullptr;

    vkCmdBeginRendering(cmd, &rendering);
}

internal void EndPass(VkCommandBuffer cmd)
{
    vkCmdEndRendering(cmd);
}

internal void GpuBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkMemoryBarrier2 barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask  = dstStage;
    barrier.dstAccessMask = dstAccess;

    VkDependencyInfo dependency{};
    dependency.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependency);
}
