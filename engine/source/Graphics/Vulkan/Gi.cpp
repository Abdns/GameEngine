#include "Vulkan.h"

internal void CreateGiVolume(vulkan_context *context, vulkan_resources *res, VkCommandBuffer cmd, uint32 slot, uint32 width, uint32 height, uint32 depth)
{
    gpu_image *volume = CreateVolume(context, res, slot, width, height, depth);
    WriteHeapImage(context, &res->Heap, BINDING_VOLUMES, slot, volume->View);
    WriteHeapImage(context, &res->Heap, BINDING_STORAGE_VOLUMES, slot, volume->View);
    CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CmdClearImage(cmd, volume->Image);
}

internal void CreateGiResources(vulkan_context *context, vulkan_resources *res, VkCommandBuffer cmd)
{
    CreateGiVolume(context, res, cmd, VOLUME_SLOT_ALBEDO,                VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE);
    CreateGiVolume(context, res, cmd, VOLUME_SLOT_NORMAL,                VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE);

    CreateGiVolume(context, res, cmd, VOLUME_SLOT_RADIANCE,        LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE);
    CreateGiVolume(context, res, cmd, VOLUME_SLOT_RADIANCE_SMOOTH, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE);

    for (uint32 i = RC_SCREEN_HANDOFF; i < RC_CASCADE_COUNT; ++i)
    {
        CreateGiVolume(context, res, cmd, VOLUME_SLOT_CASCADE + i, RC_TILE_SIZE, RC_TILE_SIZE, RC_CASCADE_PROBE_SIZE(i));
    }

    for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
    {
        CreateGiVolume(context, res, cmd, VOLUME_SLOT_IRRADIANCE + i, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE);
    }

    CreateGiVolume(context, res, cmd, VOLUME_SLOT_SCREEN_GI, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1);
    CreateGiVolume(context, res, cmd, VOLUME_SLOT_ENVIRONMENT, GI_ENV_SIZE, GI_ENV_SIZE, GI_ENV_LAYERS);
    res->GiEnvironmentDirty = true;

    CreateGiVolume(context, res, cmd, VOLUME_SLOT_SCREEN_META, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1);

    CreateGiVolume(context, res, cmd, VOLUME_SLOT_HANDOFF, RC_HANDOFF_TILE_SIZE, RC_HANDOFF_TILE_SIZE, RC_HANDOFF_PROBE_SIZE);

    for (uint32 i = 0; i < MAX_UINT_VOLUMES; ++i)
    {
        gpu_image *volume = CreateUintVolume(context, res, i, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE);
        WriteHeapImage(context, &res->Heap, BINDING_UINT_VOLUMES, i, volume->View);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        CmdClearImage(cmd, volume->Image);
    }

    // Make zero-initialized history available to the first GI and scene passes.
    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

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
    for (uint32 slot = 0; slot < MAX_UINT_VOLUMES; ++slot)
    {
        CmdClearImage(cmd, res->UintVolumes[slot].Image);
    }
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

        voxelize_params params = {};
        params.Model         = meshCmd->Transform;
        params.Tint          = meshCmd->Tint;
        params.Vertices      = res->VertexBuffer.Address;
        params.Indices       = res->IndexBuffer.Address;
        params.Materials     = res->MaterialBuffer.Address;
        params.FirstIndex    = mesh->FirstIndex;
        params.TriangleCount = mesh->IndexCount / 3;
        params.FirstVertex   = mesh->FirstVertex;
        params.MaterialSlot  = materialSlot;

        PushPassParams(cmd, res, params);

        uint32 groupCount = (params.TriangleCount + VOXEL_GROUP_SIZE - 1) / VOXEL_GROUP_SIZE;
        DispatchCompute(cmd, groupCount, 1, 1);
    }
}

internal void ResolveVoxels(vulkan_context *context, VkCommandBuffer cmd, compute_pipeline *pipeline)
{
    uint32 groupCount = VOLUME_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchVolumePass(context, cmd, pipeline, groupCount, groupCount, groupCount);
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

internal void UpdateWorldGi(vulkan_context *context, vulkan_frame *frame, vulkan_resources *res, vulkan_pipelines *pipelines, render_commands *commands)
{
    VkCommandBuffer cmd = frame->Cmd;

    // Serialize writes to shared GI volumes against last frame's fragment reads.
    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
               VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);

    frame_globals *globals = (frame_globals *)res->Globals.Cpu;
    {
        GpuSection(context, frame, "environment");
        if (res->GiEnvironmentDirty || res->GiEnvironmentSky != globals->SkyCubemap)
        {
            DispatchVolumePass(context, cmd, &pipelines->Compute[Compute_EnvironmentPrefilter], GI_ENV_SIZE / 8, GI_ENV_SIZE / 8, GI_ENV_LAYERS);
            StorageBarrier(cmd);
            res->GiEnvironmentSky = globals->SkyCubemap;
            res->GiEnvironmentDirty = false;
        }
    }

    {
        GpuSection(context, frame, "vox");

        GpuBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        ClearVoxelGrids(cmd, res);
        GpuBarrier(cmd, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        VoxelizeMeshes(context, cmd, res, &pipelines->Compute[Compute_VoxelizeMesh], commands);
        StorageBarrier(cmd);
        ResolveVoxels(context, cmd, &pipelines->Compute[Compute_VoxelizeResolve]);
    }

    {
        GpuSection(context, frame, "inject");

        StorageBarrier(cmd);
        InjectRadiance(context, cmd, &pipelines->Compute[Compute_RadianceInject]);
        StorageBarrier(cmd);
        SmoothRadiance(context, cmd, &pipelines->Compute[Compute_RadianceSmooth]);
    }

    {
        GpuSection(context, frame, "cascades");

        StorageBarrier(cmd);
        TraceCascades(context, cmd, res, &pipelines->Compute[Compute_CascadesTrace]);
        MergeCascades(context, cmd, res, &pipelines->Compute[Compute_CascadesMerge]);
        StorageBarrier(cmd);
        ResolveIrradiance(context, cmd, &pipelines->Compute[Compute_CascadesResolve]);
        PrefilterHandoff(context, cmd, &pipelines->Compute[Compute_CascadesPrefilter]);
    }

    StorageBarrier(cmd);
}

internal void UpdateScreenGi(vulkan_context *context, vulkan_frame *frame, vulkan_pipelines *pipelines)
{
    VkCommandBuffer cmd = frame->Cmd;

    GpuSection(context, frame, "screen");

    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    ComputeScreenGI(context, cmd, &pipelines->Compute[Compute_ScreenGiProbe]);
    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}
