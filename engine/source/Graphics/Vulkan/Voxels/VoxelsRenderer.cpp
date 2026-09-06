#include "Vulkan.h"

global_variable bool32 IrradianceReady;

internal void ClearVolume(VkCommandBuffer cmd, vulkan_resources *res, uint32 volumeSlot, uint32 volumeSize)
{
    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(volume_params), 16);

    volume_params params = {};
    params.VolumeSlot = volumeSlot;
    params.VolumeSize = volumeSize;

    *(volume_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = volumeSize / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
}

internal void ClearVoxelGrids(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *clear, compute_pipeline *uintClear)
{
    if (clear->Compute == VK_NULL_HANDLE || uintClear->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    uint32 voxelGroups = VOLUME_GRID_SIZE / VOLUME_GROUP_SIZE;

    BindComputePipeline(context, cmd, uintClear);
    DispatchCompute(cmd, voxelGroups, voxelGroups, voxelGroups);

    if (!IrradianceReady)
    {
        BindComputePipeline(context, cmd, clear);

        for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
        {
            ClearVolume(cmd, res, VOLUME_SLOT_IRRADIANCE + i, RC_IRRADIANCE_SIZE);
        }

        ClearVolume(cmd, res, VOLUME_SLOT_RADIANCE, LIGHT_GRID_SIZE);

        IrradianceReady = true;
    }

    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

internal void VoxelizeMeshes(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline, render_commands *commands)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    frame_globals *globals = (frame_globals *)res->Globals.Cpu;

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

        shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(voxelize_params), 16);

        voxelize_params params = {};
        params.Model         = meshCmd->Transform;
        params.Vertices      = res->VertexBuffer.Address;
        params.Indices       = res->IndexBuffer.Address;
        params.Materials     = res->MaterialBuffer.Address;
        params.FirstIndex    = mesh->FirstIndex;
        params.TriangleCount = mesh->IndexCount / 3;
        params.GridCenter    = globals->VolumeCenter;
        params.GridExtent    = VOLUME_WORLD_EXTENT;
        params.FirstVertex   = mesh->FirstVertex;
        params.MaterialSlot  = materialSlot;
        params.VolumeSlot    = VOLUME_SLOT_ALBEDO;
        params.GridSize      = VOLUME_GRID_SIZE;
        params.NormalSlot    = VOLUME_SLOT_NORMAL;

        *(voxelize_params *)alloc.Cpu = params;

        BindParams(cmd, res->PipelineLayout, alloc.Gpu);

        uint32 groupCount = (params.TriangleCount + VOXEL_GROUP_SIZE - 1) / VOXEL_GROUP_SIZE;

        DispatchCompute(cmd, groupCount, 1, 1);
    }
}

internal void ResolveVoxels(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    StorageBarrier(cmd);

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(volume_op_params), 16);

    volume_op_params params = {};
    params.SrcSlot = VOLUME_SLOT_ALBEDO;
    params.DstSlot = VOLUME_SLOT_NORMAL;
    params.Size    = VOLUME_GRID_SIZE;

    *(volume_op_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 voxelGroups = VOLUME_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, voxelGroups, voxelGroups, voxelGroups);
}

internal void ComputeSkyOcclusion(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(volume_op_params), 16);

    volume_op_params params = {};
    params.SrcSlot = VOLUME_SLOT_ALBEDO;
    params.DstSlot = VOLUME_SLOT_SKY_OCCLUSION;
    params.Size    = VOLUME_GRID_SIZE;

    *(volume_op_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = VOLUME_GRID_SIZE / 8;

    DispatchCompute(cmd, groupCount, 1, groupCount);
}

internal void BlurSkyOcclusion(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline, uint32 sourceSlot, uint32 targetSlot)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(volume_op_params), 16);

    volume_op_params params = {};
    params.SrcSlot = sourceSlot;
    params.DstSlot = targetSlot;
    params.Size    = VOLUME_GRID_SIZE;

    *(volume_op_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = VOLUME_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
}

internal void RcCascadeInterval(uint32 cascade, real32 *start, real32 *length)
{
    real32 base = (2.0f * VOLUME_WORLD_EXTENT) / (real32)RC_PROBE_SIZE;

    real32 scale = 1.0f;
    real32 begin = 0.0f;

    for (uint32 i = 0; i < cascade; ++i)
    {
        begin += base * scale;
        scale *= RC_INTERVAL_SCALE;
    }

    *start  = begin;
    *length = base * scale;
}

internal void InjectRadiance(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(rc_inject_params), 16);

    rc_inject_params params = {};
    params.SolidSlot      = VOLUME_SLOT_ALBEDO;
    params.NormalSlot     = VOLUME_SLOT_NORMAL;
    params.RadianceSlot   = VOLUME_SLOT_RADIANCE;
    params.IrradianceSlot = VOLUME_SLOT_IRRADIANCE;
    params.SkySlot        = VOLUME_SLOT_SKY_OCCLUSION;
    params.LightSize      = LIGHT_GRID_SIZE;

    *(rc_inject_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
}

internal void SmoothRadiance(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(volume_op_params), 16);

    volume_op_params params = {};
    params.SrcSlot = VOLUME_SLOT_RADIANCE;
    params.DstSlot = VOLUME_SLOT_RADIANCE_SMOOTH;
    params.Size    = LIGHT_GRID_SIZE;

    *(volume_op_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
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
        real32 start  = 0.0f;
        real32 length = 0.0f;

        RcCascadeInterval(cascade, &start, &length);

        shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(rc_trace_params), 16);

        rc_trace_params params = {};
        params.RadianceSlot   = VOLUME_SLOT_RADIANCE_SMOOTH;
        params.CascadeSlot    = VOLUME_SLOT_CASCADE + cascade;
        params.ProbeSize      = RC_PROBE_SIZE >> cascade;
        params.DirRes         = RC_DIR_RES << cascade;
        params.Steps          = RC_BASE_STEPS << cascade;
        params.IntervalStart  = start;
        params.IntervalLength = length;

        *(rc_trace_params *)alloc.Cpu = params;

        BindParams(cmd, res->PipelineLayout, alloc.Gpu);

        DispatchCompute(cmd, tileGroups, tileGroups, params.ProbeSize);
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
        uint32 parent = cascade - 1;

        StorageBarrier(cmd);

        shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(rc_merge_params), 16);

        rc_merge_params params = {};
        params.ParentSlot      = VOLUME_SLOT_CASCADE + parent;
        params.ChildSlot       = VOLUME_SLOT_CASCADE + cascade;
        params.ParentProbeSize = RC_PROBE_SIZE >> parent;
        params.ParentDirRes    = RC_DIR_RES << parent;
        params.ChildProbeSize  = RC_PROBE_SIZE >> cascade;
        params.RadianceSlot    = VOLUME_SLOT_RADIANCE_SMOOTH;

        *(rc_merge_params *)alloc.Cpu = params;

        BindParams(cmd, res->PipelineLayout, alloc.Gpu);

        DispatchCompute(cmd, tileGroups, tileGroups, params.ParentProbeSize);
    }
}

internal void PrefilterHandoff(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(volume_op_params), 16);

    volume_op_params params = {};
    params.SrcSlot = VOLUME_SLOT_CASCADE + RC_SCREEN_HANDOFF + 1;
    params.DstSlot = VOLUME_SLOT_HANDOFF;
    params.SrcRes  = RC_DIR_RES << (RC_SCREEN_HANDOFF + 1);
    params.DstRes  = RC_SCREEN_DIR_RES;
    params.Size    = RC_PROBE_SIZE >> (RC_SCREEN_HANDOFF + 1);

    *(volume_op_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 tile = params.Size * RC_SCREEN_DIR_RES;

    uint32 groupCount = (tile + RC_GROUP_SIZE - 1) / RC_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, params.Size);
}

internal void ResolveIrradiance(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(rc_resolve_params), 16);

    rc_resolve_params params = {};
    params.CascadeSlot    = VOLUME_SLOT_CASCADE + RC_SCREEN_HANDOFF;
    params.IrradianceSlot = VOLUME_SLOT_IRRADIANCE;
    params.ProbeSize      = RC_PROBE_SIZE >> RC_SCREEN_HANDOFF;
    params.DirRes         = RC_DIR_RES << RC_SCREEN_HANDOFF;
    params.RadianceSlot   = VOLUME_SLOT_RADIANCE_SMOOTH;

    *(rc_resolve_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = RC_IRRADIANCE_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
}


internal void ComputeScreenGI(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    uint32 probeCountX = (context->swapchainExtent.width  + RC_SCREEN_TILE - 1) / RC_SCREEN_TILE;
    uint32 probeCountY = (context->swapchainExtent.height + RC_SCREEN_TILE - 1) / RC_SCREEN_TILE;

    if (probeCountX > RC_SCREEN_MAX_X)
    {
        probeCountX = RC_SCREEN_MAX_X;
    }

    if (probeCountY > RC_SCREEN_MAX_Y)
    {
        probeCountY = RC_SCREEN_MAX_Y;
    }

    real32 start  = 0.0f;
    real32 length = 0.0f;

    RcCascadeInterval(RC_SCREEN_HANDOFF + 1, &start, &length);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(rc_screen_params), 16);

    rc_screen_params params = {};
    params.RadianceSlot   = VOLUME_SLOT_RADIANCE_SMOOTH;
    params.CascadeSlot    = VOLUME_SLOT_HANDOFF;
    params.ScreenSlot     = VOLUME_SLOT_SCREEN_GI;
    params.MetaSlot       = VOLUME_SLOT_SCREEN_META;
    params.DepthSlot      = TEXTURE_SLOT_DEPTH;
    params.ProbeSize      = RC_PROBE_SIZE >> (RC_SCREEN_HANDOFF + 1);
    params.DirRes         = RC_SCREEN_DIR_RES;
    params.Steps          = RC_BASE_STEPS << (RC_SCREEN_HANDOFF + 1);
    params.IntervalLength = start;
    params.ProbeCountX    = probeCountX;
    params.ProbeCountY    = probeCountY;

    *(rc_screen_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupX = (probeCountX + RC_SCREEN_GROUP - 1) / RC_SCREEN_GROUP;
    uint32 groupY = (probeCountY + RC_SCREEN_GROUP - 1) / RC_SCREEN_GROUP;

    DispatchCompute(cmd, groupX, groupY, 1);
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

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(volume_params), 16);

    volume_params params = {};
    params.VolumeSlot      = volumeSlot;
    params.VolumeSize       = volumeSize;
    params.VolumeSlice      = slice;
    params.VolumeMode       = mode;
    params.VolumeLightSlot  = VOLUME_SLOT_IRRADIANCE;

    *(volume_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    VkViewport full{};
    full.width    = (real32)context->swapchainExtent.width;
    full.height   = (real32)context->swapchainExtent.height;
    full.maxDepth = 1.0f;
    vkCmdSetViewportWithCount(cmd, 1, &full);
}
