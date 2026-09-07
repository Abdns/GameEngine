#include "Vulkan.h"

internal void DispatchVolumePass(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline, uint32 countX, uint32 countY, uint32 countZ)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    DispatchCompute(cmd, countX, countY, countZ);
}

internal void ClearVoxelGrids(VkCommandBuffer cmd, vulkan_resources *res)
{
    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    for (uint32 slot = 0; slot < MAX_UINT_VOLUMES; ++slot)
    {
        CmdClearImage(cmd, res->UintVolumes[slot].Image);
    }

    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

internal void VoxelizeMeshes(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline, render_commands *commands)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    uint32 offset = 0;
    for (command_type *cmdBase = NextRenderCommand(commands, &offset); cmdBase; cmdBase = NextRenderCommand(commands, &offset))
    {
        if (*cmdBase != Render_Mesh)
        {
            continue;
        }

        command_render_mesh *meshCmd = (command_render_mesh *)cmdBase;
        Assert(meshCmd->MeshHandle < MAX_MESHES);

        gpu_mesh *mesh = res->Meshes + meshCmd->MeshHandle;
        if (!mesh->IndexCount)
        {
            continue;
        }

        uint32 materialSlot = meshCmd->MaterialHandle;
        Assert(materialSlot < res->MaterialCount);

        material_state *material = &res->MaterialStates[materialSlot];
        if (material->Queue != Queue_Opaque || material->Pipeline != Pipeline_Lit)
        {
            continue;
        }

        gpu_alloc alloc = BufferAlloc(&res->FrameArena, sizeof(voxelize_params), 16);

        voxelize_params params = {};
        params.Model         = meshCmd->Transform;
        params.Vertices      = res->VertexBuffer.Address;
        params.Indices       = res->IndexBuffer.Address;
        params.Materials     = res->MaterialBuffer.Address;
        params.FirstIndex    = mesh->FirstIndex;
        params.TriangleCount = mesh->IndexCount / 3;
        params.FirstVertex   = mesh->FirstVertex;
        params.MaterialSlot  = materialSlot;

        *(voxelize_params *)alloc.Cpu = params;

        BindParams(cmd, res->PipelineLayout, alloc.Gpu);

        uint32 groupCount = (params.TriangleCount + VOXEL_GROUP_SIZE - 1) / VOXEL_GROUP_SIZE;

        DispatchCompute(cmd, groupCount, 1, 1);
    }
}

internal void ResolveVoxels(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    StorageBarrier(cmd);

    uint32 groupCount = VOLUME_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchVolumePass(context, cmd, pipeline, groupCount, groupCount, groupCount);
}

internal void ComputeSkyOcclusion(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    uint32 groupCount = VOLUME_GRID_SIZE / 8;

    DispatchVolumePass(context, cmd, pipeline, groupCount, 1, groupCount);
}

internal void BlurSkyOcclusion(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline, uint32 sourceSlot, uint32 targetSlot)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    volume_op_params params = {};
    params.SrcSlot = sourceSlot;
    params.DstSlot = targetSlot;

    PushPassParams(cmd, res, params);

    uint32 groupCount = VOLUME_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
}

internal void InjectRadiance(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchVolumePass(context, cmd, pipeline, groupCount, groupCount, groupCount);
}

internal void SmoothRadiance(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchVolumePass(context, cmd, pipeline, groupCount, groupCount, groupCount);
}

internal void TraceCascades(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    uint32 tileGroups = RC_TILE_SIZE / RC_GROUP_SIZE;

    for (uint32 cascade = RC_SCREEN_HANDOFF; cascade < RC_CASCADE_COUNT; ++cascade)
    {
        rc_cascade_params params = {};
        params.Cascade = cascade;

        PushPassParams(cmd, res, params);

        DispatchCompute(cmd, tileGroups, tileGroups, RC_CASCADE_PROBE_SIZE(cascade));
    }
}

internal void MergeCascades(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    uint32 tileGroups = RC_TILE_SIZE / RC_GROUP_SIZE;

    for (uint32 cascade = RC_CASCADE_COUNT - 1; cascade > RC_SCREEN_HANDOFF; --cascade)
    {
        StorageBarrier(cmd);

        rc_cascade_params params = {};
        params.Cascade = cascade;

        PushPassParams(cmd, res, params);

        DispatchCompute(cmd, tileGroups, tileGroups, RC_CASCADE_PROBE_SIZE(cascade - 1));
    }
}

internal void PrefilterHandoff(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    uint32 groupCount = (RC_HANDOFF_TILE_SIZE + RC_GROUP_SIZE - 1) / RC_GROUP_SIZE;

    DispatchVolumePass(context, cmd, pipeline, groupCount, groupCount, RC_HANDOFF_PROBE_SIZE);
}

internal void ResolveIrradiance(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    uint32 groupCount = RC_IRRADIANCE_SIZE / VOLUME_GROUP_SIZE;

    DispatchVolumePass(context, cmd, pipeline, groupCount, groupCount, groupCount);
}

internal void ComputeScreenGI(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    uint32 probeCountX = RC_SCREEN_PROBES_X(context->swapchainExtent.width);
    uint32 probeCountY = RC_SCREEN_PROBES_Y(context->swapchainExtent.height);

    uint32 groupX = (probeCountX + RC_SCREEN_GROUP - 1) / RC_SCREEN_GROUP;
    uint32 groupY = (probeCountY + RC_SCREEN_GROUP - 1) / RC_SCREEN_GROUP;

    DispatchVolumePass(context, cmd, pipeline, groupX, groupY, 1);
}

internal void DrawVolumeDebug(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, render_pipeline *pipeline, uint32 volumeSlot, uint32 volumeSize, uint32 mode, real32 slice)
{
    if (pipeline->Vert == VK_NULL_HANDLE)
    {
        return;
    }

    render_state current = {};
    render_state wanted  = {};
    BindPipelineState(context, cmd, pipeline, &current, &wanted);

    real32 insetWidth = 320.0f;

    VkViewport inset{};
    inset.x        = 16.0f;
    inset.y        = 16.0f;
    inset.width    = insetWidth;
    inset.height   = insetWidth * (real32)context->swapchainExtent.height / (real32)context->swapchainExtent.width;
    inset.maxDepth = 1.0f;
    vkCmdSetViewportWithCount(cmd, 1, &inset);

    volume_params params = {};
    params.VolumeSlot      = volumeSlot;
    params.VolumeSize      = volumeSize;
    params.VolumeSlice     = slice;
    params.VolumeMode      = mode;
    params.VolumeLightSlot = VOLUME_SLOT_IRRADIANCE;

    PushPassParams(cmd, res, params);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    VkViewport full{};
    full.width    = (real32)context->swapchainExtent.width;
    full.height   = (real32)context->swapchainExtent.height;
    full.maxDepth = 1.0f;
    vkCmdSetViewportWithCount(cmd, 1, &full);
}
