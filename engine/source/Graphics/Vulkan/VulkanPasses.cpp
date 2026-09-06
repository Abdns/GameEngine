#include "Vulkan.h"

enum depth_mode
{
    Depth_None = 0,
    Depth_Clear,
    Depth_Load,
};

global_variable gpu_image DepthTarget;
global_variable gpu_image SceneTarget;
global_variable gpu_image PostTarget;

internal void CreateDepthResources(vulkan_context *context, vulkan_resources *res, VkCommandBuffer cmd)
{
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    DepthTarget = CreateImage(context, Image_DepthTarget, depthFormat, context->swapchainExtent.width, context->swapchainExtent.height, 1, 1);

    CmdImageToGeneral(cmd, DepthTarget.Image, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);

    WriteImageDescriptor(context, &res->Heap, res->Heap.TextureOffset, TEXTURE_SLOT_DEPTH, DepthTarget.View, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
}

internal gpu_image CreateRenderTarget(vulkan_context *context, vulkan_resources *res, uint32 textureSlot, VkFormat format, VkCommandBuffer cmd)
{
    gpu_image target = CreateImage(context, Image_ColorTarget, format, context->swapchainExtent.width, context->swapchainExtent.height, 1, 1);

    CmdImageToGeneral(cmd, target.Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);

    WriteImageDescriptor(context, &res->Heap, res->Heap.TextureOffset, textureSlot, target.View, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

    return target;
}

internal void BeginPass(vulkan_context *context, VkCommandBuffer cmd, VkImageView target, VkAttachmentLoadOp colorLoad, Vector4 clearColor, depth_mode depthMode)
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
    depth.imageView                    = DepthTarget.View;
    depth.imageLayout                  = VK_IMAGE_LAYOUT_GENERAL;
    depth.loadOp                       = (depthMode == Depth_Clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp                      = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo rendering{};
    rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent    = context->swapchainExtent;
    rendering.layerCount           = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments    = &color;
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

internal void StorageBarrier(VkCommandBuffer cmd)
{
    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
