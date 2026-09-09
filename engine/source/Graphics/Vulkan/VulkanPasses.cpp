#include "Vulkan.h"

enum depth_mode
{
    Depth_None = 0,
    Depth_Clear,
    Depth_Load,
};

internal gpu_image CreateDepthTarget(vulkan_context *context, VkCommandBuffer cmd)
{
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    gpu_image target = CreateImage(context, Image_DepthTarget, depthFormat, context->swapchainExtent.width, context->swapchainExtent.height, 1, 1);

    CmdImageToGeneral(cmd, target.Image, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);

    return target;
}

internal gpu_image CreateRenderTarget(vulkan_context *context, descriptor_heap *heap, uint32 textureSlot, VkFormat format, VkCommandBuffer cmd)
{
    gpu_image target = CreateImage(context, Image_ColorTarget, format, context->swapchainExtent.width, context->swapchainExtent.height, 1, 1);

    CmdImageToGeneral(cmd, target.Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);

    WriteHeapImage(context, heap, BINDING_TEXTURES, textureSlot, target.View);

    return target;
}

internal frame_targets CreateFrameTargets(vulkan_context *context, descriptor_heap *heap, VkCommandBuffer cmd)
{
    frame_targets targets = {};
    targets.Depth = CreateDepthTarget(context, cmd);
    targets.Scene = CreateRenderTarget(context, heap, TEXTURE_SLOT_SCENE, VK_FORMAT_R16G16B16A16_SFLOAT, cmd);
    targets.Post  = CreateRenderTarget(context, heap, TEXTURE_SLOT_POST,  VK_FORMAT_R16G16B16A16_SFLOAT, cmd);

    return targets;
}

internal void DestroyFrameTargets(vulkan_context *context, frame_targets *targets)
{
    DestroyImage(context, &targets->Depth);
    DestroyImage(context, &targets->Scene);
    DestroyImage(context, &targets->Post);
}

internal void BeginPass(vulkan_context *context, VkCommandBuffer cmd, VkImageView colorTarget, VkImageView depthTarget, VkAttachmentLoadOp colorLoad, Vector4 clearColor, depth_mode depthMode)
{
    VkRenderingAttachmentInfo color{};
    color.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView   = colorTarget;
    color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    color.loadOp      = colorLoad;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    color.clearValue.color.float32[0] = clearColor.X;
    color.clearValue.color.float32[1] = clearColor.Y;
    color.clearValue.color.float32[2] = clearColor.Z;
    color.clearValue.color.float32[3] = clearColor.W;

    VkRenderingAttachmentInfo depth{};
    depth.sType                        = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView                    = depthTarget;
    depth.imageLayout                  = VK_IMAGE_LAYOUT_GENERAL;
    depth.loadOp                       = (depthMode == Depth_Clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp                      = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo rendering{};
    rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent    = context->swapchainExtent;
    rendering.layerCount           = 1;
    rendering.colorAttachmentCount = colorTarget ? 1u : 0u;
    rendering.pColorAttachments    = colorTarget ? &color : nullptr;
    rendering.pDepthAttachment     = (depthMode != Depth_None) ? &depth : nullptr;

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
