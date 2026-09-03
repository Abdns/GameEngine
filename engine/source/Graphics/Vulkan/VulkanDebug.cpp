#include "Vulkan.h"

global_variable bool32 TimestampReady[MAX_FRAMES_IN_FLIGHT];
global_variable real64 GpuSectionAccum[8];
global_variable uint32 GpuSampleCount;

internal void CreateTimestampPool(vulkan_context *context)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context->physicalDevice, &props);

    context->timestampPeriod = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo info{};
    info.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = MAX_FRAMES_IN_FLIGHT * 16;

    VkResult result = vkCreateQueryPool(context->device, &info, nullptr, &context->timestampPool);
    Assert(result == VK_SUCCESS);
}

internal void GpuStamp(vulkan_context *context, VkCommandBuffer cmd, uint32 base, uint32 index)
{
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->timestampPool, base + index);
}

internal void ResetGpuTimestamps(vulkan_context *context, VkCommandBuffer cmd, uint32 stampBase)
{
    vkCmdResetQueryPool(cmd, context->timestampPool, stampBase, 16);
}

internal void ReadGpuTimestamps(vulkan_context *context, uint32 slot, uint32 stampBase)
{
    if (!TimestampReady[slot])
    {
        return;
    }

    uint64 stamps[9];

    if (vkGetQueryPoolResults(context->device, context->timestampPool, stampBase, 9, sizeof(stamps), stamps, sizeof(uint64), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    {
        return;
    }

    for (uint32 i = 0; i < 8; ++i)
    {
        GpuSectionAccum[i] += (real64)(stamps[i + 1] - stamps[i]) * (real64)context->timestampPeriod * 1e-6;
    }

    ++GpuSampleCount;

    if (GpuSampleCount >= 120)
    {
        real64 scale = 1.0 / (real64)GpuSampleCount;
        real64 total = 0.0;

        for (uint32 i = 0; i < 8; ++i)
        {
            GpuSectionAccum[i] *= scale;
            total += GpuSectionAccum[i];
        }

        DebugLog("[gpu] vox %.2f sky %.2f inject %.2f cascades %.2f depth %.2f screen %.2f scene %.2f post %.2f | total %.2f ms\n", GpuSectionAccum[0], GpuSectionAccum[1], GpuSectionAccum[2], GpuSectionAccum[3], GpuSectionAccum[4], GpuSectionAccum[5], GpuSectionAccum[6], GpuSectionAccum[7], total);

        for (uint32 i = 0; i < 8; ++i)
        {
            GpuSectionAccum[i] = 0.0;
        }

        GpuSampleCount = 0;
    }
}
