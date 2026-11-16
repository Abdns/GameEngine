#include "Vulkan.h"

#define GPU_STAMPS_PER_FRAME 32
#define MAX_GPU_SECTIONS     (GPU_STAMPS_PER_FRAME / 2)
#define GPU_SAMPLE_WINDOW    120

global_variable bool32      TimestampReady[MAX_FRAMES_IN_FLIGHT];
global_variable const char *GpuSectionNames[MAX_GPU_SECTIONS];
global_variable real64      GpuSectionAccum[MAX_GPU_SECTIONS];
global_variable uint32      GpuSectionCount;
global_variable uint32      GpuSampleCount;

internal void CreateTimestampPool(vulkan_context *context)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context->physicalDevice, &props);

    context->timestampPeriod = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo info{};
    info.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = MAX_FRAMES_IN_FLIGHT * GPU_STAMPS_PER_FRAME;

    VkResult result = vkCreateQueryPool(context->device, &info, nullptr, &context->timestampPool);
    Assert(result == VK_SUCCESS);
}

internal void ReportGpuSections(void)
{
    real64 scale = 1.0 / (real64)GpuSampleCount;
    real64 total = 0.0;

    DebugLog("[gpu]");

    for (uint32 i = 0; i < GpuSectionCount; ++i)
    {
        real64 millis = GpuSectionAccum[i] * scale;

        total += millis;

        DebugLog(" %s %.2f", GpuSectionNames[i], millis);

        GpuSectionAccum[i] = 0.0;
    }

    DebugLog(" | total %.2f ms\n", total);

    GpuSampleCount = 0;
}

internal void ReadGpuTimestamps(vulkan_context *context, uint32 slot)
{
    if (!TimestampReady[slot] || !GpuSectionCount)
    {
        return;
    }

    uint64 stamps[GPU_STAMPS_PER_FRAME];

    uint32 stampCount = GpuSectionCount * 2;

    if (vkGetQueryPoolResults(context->device, context->timestampPool, slot * GPU_STAMPS_PER_FRAME, stampCount, stampCount * sizeof(uint64), stamps, sizeof(uint64), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    {
        return;
    }

    for (uint32 i = 0; i < GpuSectionCount; ++i)
    {
        GpuSectionAccum[i] += (real64)(stamps[i * 2 + 1] - stamps[i * 2]) * (real64)context->timestampPeriod * 1e-6;
    }

    ++GpuSampleCount;

    if (GpuSampleCount >= GPU_SAMPLE_WINDOW)
    {
        ReportGpuSections();
    }
}

internal void BeginGpuFrame(vulkan_context *context, vulkan_frame *frame)
{
    ReadGpuTimestamps(context, frame->Slot);

    vkCmdResetQueryPool(frame->Cmd, context->timestampPool, frame->Slot * GPU_STAMPS_PER_FRAME, GPU_STAMPS_PER_FRAME);

    GpuSectionCount = 0;
}

internal void EndGpuFrame(vulkan_frame *frame)
{
    TimestampReady[frame->Slot] = true;
}

struct gpu_section_scope
{
    vulkan_context *Context;
    VkCommandBuffer Cmd;
    uint32          Query;

    gpu_section_scope(vulkan_context *context, vulkan_frame *frame, const char *name)
    {
        Assert(GpuSectionCount < MAX_GPU_SECTIONS);

        Context = context;
        Cmd     = frame->Cmd;
        Query   = frame->Slot * GPU_STAMPS_PER_FRAME + GpuSectionCount * 2;

        GpuSectionNames[GpuSectionCount] = name;

        ++GpuSectionCount;

        vkCmdWriteTimestamp(Cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Context->timestampPool, Query);
    }

    ~gpu_section_scope()
    {
        vkCmdWriteTimestamp(Cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Context->timestampPool, Query + 1);
    }
};

#define GpuSectionName(Line) GpuSectionJoin(GpuSectionScope, Line)
#define GpuSectionJoin(A, B) A##B

#define GpuSection(Context, Frame, Name) gpu_section_scope GpuSectionName(__LINE__)(Context, Frame, Name)
