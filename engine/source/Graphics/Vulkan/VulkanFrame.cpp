#include "Vulkan.h"

internal VkCullModeFlags ToVulkanCullMode(cull_mode mode)
{
    switch (mode)
    {
        case Cull_Back:  return VK_CULL_MODE_BACK_BIT;
        case Cull_Front: return VK_CULL_MODE_FRONT_BIT;
        case Cull_None:  return VK_CULL_MODE_NONE;
    }

    return VK_CULL_MODE_NONE;
}

internal void ApplyRenderState(vulkan_context *context, VkCommandBuffer cmd, render_state *current, render_state *wanted)
{
    if (!current->Valid || current->CullMode != wanted->CullMode)
    {
        vkCmdSetCullMode(cmd, wanted->CullMode);
        current->CullMode = wanted->CullMode;
    }

    if (!current->Valid || current->DepthTest != wanted->DepthTest)
    {
        vkCmdSetDepthTestEnable(cmd, wanted->DepthTest);
        current->DepthTest = wanted->DepthTest;
    }

    if (!current->Valid || current->DepthWrite != wanted->DepthWrite)
    {
        vkCmdSetDepthWriteEnable(cmd, wanted->DepthWrite);
        current->DepthWrite = wanted->DepthWrite;
    }

    if (!current->Valid || current->AlphaBlend != wanted->AlphaBlend)
    {
        VkBool32 enable = wanted->AlphaBlend;
        context->CmdSetColorBlendEnableEXT(cmd, 0, 1, &enable);
        current->AlphaBlend = wanted->AlphaBlend;
    }

    current->Valid = true;
}

internal void ApplyFixedState(vulkan_context *context, VkCommandBuffer cmd)
{
    context->CmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

    vkCmdSetPrimitiveTopology(cmd, PIPELINE_TOPOLOGY);
    vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);
    vkCmdSetFrontFace(cmd, PIPELINE_FRONT_FACE);
    vkCmdSetRasterizerDiscardEnable(cmd, VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS_OR_EQUAL);
    vkCmdSetDepthBiasEnable(cmd, VK_FALSE);
    vkCmdSetDepthBoundsTestEnable(cmd, VK_FALSE);
    vkCmdSetStencilTestEnable(cmd, VK_FALSE);

    context->CmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_FILL);
    context->CmdSetDepthClampEnableEXT(cmd, VK_FALSE);
    context->CmdSetLogicOpEnableEXT(cmd, VK_FALSE);
    context->CmdSetRasterizationSamplesEXT(cmd, VK_SAMPLE_COUNT_1_BIT);
    context->CmdSetAlphaToCoverageEnableEXT(cmd, VK_FALSE);

    VkSampleMask sampleMask = 0xFFFFFFFF;
    context->CmdSetSampleMaskEXT(cmd, VK_SAMPLE_COUNT_1_BIT, &sampleMask);

    VkColorComponentFlags writeMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    context->CmdSetColorWriteMaskEXT(cmd, 0, 1, &writeMask);

    VkColorBlendEquationEXT blendEquation{};
    blendEquation.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendEquation.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendEquation.colorBlendOp        = VK_BLEND_OP_ADD;
    blendEquation.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendEquation.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendEquation.alphaBlendOp        = VK_BLEND_OP_ADD;

    context->CmdSetColorBlendEquationEXT(cmd, 0, 1, &blendEquation);
}

internal void BindParams(VkCommandBuffer cmd, VkPipelineLayout layout, VkDeviceAddress address)
{
    vkCmdPushConstants(cmd, layout, PIPELINE_PUSH_STAGES, (uint32)offsetof(push_constants, ParamsPtr), (uint32)sizeof(address), &address);
}

internal void BindGlobals(VkCommandBuffer cmd, VkPipelineLayout layout, VkDeviceAddress address)
{
    vkCmdPushConstants(cmd, layout, PIPELINE_PUSH_STAGES, (uint32)offsetof(push_constants, GlobalsPtr), (uint32)sizeof(address), &address);
}

internal void BindPipelineState(vulkan_context *context, VkCommandBuffer cmd, render_pipeline *pipeline, render_state *current, render_state *wanted)
{
    VkShaderStageFlagBits stages[2] = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };
    VkShaderEXT shaders[2] = { pipeline->Vert, pipeline->Frag };

    context->CmdBindShadersEXT(cmd, 2, stages, shaders);

    ApplyFixedState(context, cmd);

    *wanted = pipeline->DefaultState;
    current->Valid = false;

    ApplyRenderState(context, cmd, current, wanted);
}

internal void BindComputePipeline(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    VkShaderStageFlagBits stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    VkShaderEXT shader = pipeline->Compute;

    context->CmdBindShadersEXT(cmd, 1, &stage, &shader);
}

internal void DispatchCompute(VkCommandBuffer cmd, uint32 countX, uint32 countY, uint32 countZ)
{
    vkCmdDispatch(cmd, countX, countY, countZ);
}

internal void DrawFullscreen(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, render_pipeline *pipeline, uint32 textureSlot)
{
    if (pipeline->Vert == VK_NULL_HANDLE)
    {
        return;
    }

    render_state current = {};
    render_state wanted  = {};
    BindPipelineState(context, cmd, pipeline, &current, &wanted);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(image_params), 16);

    image_params params = {};
    params.TextureSlot = textureSlot;

    *(image_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

internal void WaitForFrame(vulkan_context *context)
{
    if (context->frameIndex < MAX_FRAMES_IN_FLIGHT)
    {
        return;
    }

    uint64 waitValue = context->frameIndex - MAX_FRAMES_IN_FLIGHT + 1;

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores    = &context->frameTimeline;
    waitInfo.pValues        = &waitValue;

    vkWaitSemaphores(context->device, &waitInfo, UINT64_MAX);
}

internal vulkan_frame BeginFrame(vulkan_context *context, vulkan_resources *res)
{
    vulkan_frame Frame = {};

    Frame.Slot = (uint32)(context->frameIndex % MAX_FRAMES_IN_FLIGHT);

    VkResult acquire = vkAcquireNextImageKHR(context->device, context->swapchain, UINT64_MAX, context->imageAvailableSemaphores[Frame.Slot], VK_NULL_HANDLE, &Frame.ImageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        Frame.NeedsResize = true;
        return Frame;
    }

    if (acquire == VK_SUBOPTIMAL_KHR)
    {
        Frame.NeedsResize = true;
    }

    ResetFrameRegion(&res->FrameArena, Frame.Slot);

    res->Globals = GetSharedBufferSlot(&res->GlobalsBuffer, Frame.Slot, sizeof(frame_globals));
    *(frame_globals *)res->Globals.Cpu = {};

    VkCommandBuffer cmd = context->commandBuffers[Frame.Slot];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width  = (float)context->swapchainExtent.width;
    viewport.height = (float)context->swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewportWithCount(cmd, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = context->swapchainExtent;
    vkCmdSetScissorWithCount(cmd, 1, &scissor);

    BindGlobals(cmd, res->PipelineLayout, res->Globals.Gpu);

    Frame.Cmd   = cmd;
    Frame.Ready = true;
    return Frame;
}

internal void EndFrame(vulkan_context *context, vulkan_frame *Frame)
{
    vkEndCommandBuffer(Frame->Cmd);

    VkSemaphoreSubmitInfo waitSem{};
    waitSem.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSem.semaphore = context->imageAvailableSemaphores[Frame->Slot];
    waitSem.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalSems[2] = {};
    signalSems[0].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSems[0].semaphore = context->renderFinishedSemaphores[Frame->ImageIndex];
    signalSems[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    signalSems[1].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSems[1].semaphore = context->frameTimeline;
    signalSems[1].value     = context->frameIndex + 1;
    signalSems[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = Frame->Cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount   = 1;
    submitInfo.pWaitSemaphoreInfos      = &waitSem;
    submitInfo.commandBufferInfoCount   = 1;
    submitInfo.pCommandBufferInfos      = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = 2;
    submitInfo.pSignalSemaphoreInfos    = signalSems;

    if (vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        DebugLog("Fail to submit draw command buffer\n");
        return;
    }

    context->frameIndex++;

    VkSemaphore    signalSemaphores[] = { context->renderFinishedSemaphores[Frame->ImageIndex] };
    VkSwapchainKHR swapchains[]       = { context->swapchain };
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &Frame->ImageIndex;

    VkResult present = vkQueuePresentKHR(context->presentQueue, &presentInfo);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR)
    {
        Frame->NeedsResize = true;
    }
}
