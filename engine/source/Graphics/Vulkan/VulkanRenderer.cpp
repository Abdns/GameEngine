#include "Vulkan.h"

#include "VulkanCore.cpp"
#include "VulkanDevice.cpp"
#include "VulkanResources.cpp"
#include "VulkanPasses.cpp"
#include "VulkanPipeline.cpp"
#include "VulkanFrame.cpp"

global_variable render_pipeline  Pipelines[Pipeline_Count];
global_variable compute_pipeline ComputePipelines[Compute_Count];

global_variable bool32           IrradianceReady;
global_variable bool32           TimestampReady[MAX_FRAMES_IN_FLIGHT];
global_variable real64           GpuSectionAccum[8];
global_variable uint32           GpuSampleCount;

global_variable pipeline_desc PipelineDescs[] =
{
    { "unlit",      VK_TRUE,  VK_TRUE,  VK_FALSE },
    { "lit",        VK_TRUE,  VK_TRUE,  VK_FALSE },
    { "skybox",     VK_FALSE, VK_FALSE, VK_FALSE },
    { "post",       VK_FALSE, VK_FALSE, VK_FALSE },
    { "UI",         VK_FALSE, VK_FALSE, VK_FALSE },
    { "uirect",     VK_FALSE, VK_FALSE, VK_TRUE  },
    { "volumeview", VK_FALSE, VK_FALSE, VK_FALSE },
    { "depth",      VK_TRUE,  VK_TRUE,  VK_FALSE },
};

global_variable const char *ComputeDescs[] =
{
    "voxelclear",
    "voxelize",
    "uintclear",
    "voxelresolve",
    "skyocclusion",
    "skyocclusionblur",
    "rcinject",
    "rctrace",
    "rcmerge",
    "rcresolve",
    "rcscreen",
    "rcsmooth",
    "rcprefilter",
};

static_assert(ArrayCount(PipelineDescs) == Pipeline_Count, "PipelineDescs must describe every pipeline_type");
static_assert(ArrayCount(ComputeDescs) == Compute_Count, "ComputeDescs must describe every compute_type");

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

internal void ResizeRenderer(vulkan_context *context)
{
    if (!RecreateSwapchain(context))
    {
        return;
    }

    DestroyTexture(context, &DepthTarget);
    DestroyTexture(context, &SceneTarget);
    DestroyTexture(context, &PostTarget);

    VkCommandBuffer setup = BeginSingleTimeCommands(context);
    {
        CreateDepthResources(context, &GlobalResources, setup);

        SceneTarget = CreateRenderTarget(context, &GlobalResources, TEXTURE_SLOT_SCENE, VK_FORMAT_R16G16B16A16_SFLOAT, setup);
        PostTarget  = CreateRenderTarget(context, &GlobalResources, TEXTURE_SLOT_POST,  VK_FORMAT_R16G16B16A16_SFLOAT, setup);
    }
    EndSingleTimeCommands(context, setup);
}

internal const char *InitVulkan(HINSTANCE hinstance, HWND hwnd)
{
    vulkan_context *context = &GlobalVulkan;
    context->windowHandle = hwnd;

    if (!CheckInstanceVersion())
    {
        return "This machine has no Vulkan 1.3 loader, update the graphics drivers";
    }

    if (!CheckInstanceExtensionSupport(RequiredInstanceExtensions, ArrayCount(RequiredInstanceExtensions)))
    {
        return "This machine is missing the required Vulkan instance extensions";
    }

    const char *instanceExtensions[8];
    uint32 instanceExtensionCount = GatherInstanceExtensions(instanceExtensions, ArrayCount(instanceExtensions));

    VkApplicationInfo appInfo = AppInfo();
    VkInstanceCreateInfo instanceInfo = InstanceInfo(&appInfo, instanceExtensions, instanceExtensionCount);

#if ENGINE_INTERNAL
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo = DebugMessengerInfo();

    if (CheckInstanceLayerSupport(ValidationLayers, (uint32)ArrayCount(ValidationLayers)))
    {
        instanceInfo.enabledLayerCount   = (uint32)ArrayCount(ValidationLayers);
        instanceInfo.ppEnabledLayerNames = ValidationLayers;
        instanceInfo.pNext               = &messengerInfo;
    }
#endif

    if (vkCreateInstance(&instanceInfo, nullptr, &context->instance) != VK_SUCCESS)
    {
        return "Vulkan instance could not be created";
    }

    DebugLog("Vulkan instance created\n");

#if ENGINE_INTERNAL
    if (instanceInfo.enabledLayerCount)
    {
        CreateDebugMessenger(context);
    }
#endif

    CreateSurface(context, hinstance, hwnd);
    SelectDevice(context);

    if (!context->physicalDevice)
    {
        return "No GPU on this machine supports the features the renderer needs";
    }

    CreateLogicalDevice(context);
    CreateCommandPool(context);
    CreateCommandBuffer(context);
    CreateSyncObjects(context);
    CreateTimestampPool(context);

    CreateSwapchain(context, hwnd);
    CreateSwapchainImageViews(context);

    VkCommandBuffer setup = BeginSingleTimeCommands(context);
    {
        CreateResources(context, &GlobalResources, setup);
        CreateDepthResources(context, &GlobalResources, setup);

        SceneTarget = CreateRenderTarget(context, &GlobalResources, TEXTURE_SLOT_SCENE, VK_FORMAT_R16G16B16A16_SFLOAT, setup);
        PostTarget  = CreateRenderTarget(context, &GlobalResources, TEXTURE_SLOT_POST,  VK_FORMAT_R16G16B16A16_SFLOAT, setup);
    }
    EndSingleTimeCommands(context, setup);


    for (uint32 i = 0; i < Pipeline_Count; ++i)
    {
        CreateRenderPipeline(context, &GlobalResources, &Pipelines[i], &PipelineDescs[i]);
    }

    for (uint32 i = 0; i < Compute_Count; ++i)
    {
        CreateComputePipeline(context, &GlobalResources, &ComputePipelines[i], ComputeDescs[i]);
    }

    DebugLog("Vulkan ready\n");

    return nullptr;
}

internal void LoadAssets(vulkan_context *context, vulkan_resources *res, render_commands *commands)
{
    if (!commands->LoadCount)
    {
        return;
    }

    Assert(res->VertexBuffer.Buffer == VK_NULL_HANDLE);
    Assert(commands->VertexCount && commands->IndexCount);
    Assert(commands->MaterialCount && commands->MaterialCount <= MAX_MATERIALS);

    res->VertexBuffer   = CreateDeviceBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(vertex) * commands->VertexCount);
    res->IndexBuffer    = CreateDeviceBuffer(context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,   sizeof(uint32) * commands->IndexCount);
    res->MaterialBuffer = CreateDeviceBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(gpu_material) * commands->MaterialCount);
    res->MaterialCount  = commands->MaterialCount;

    shared_buffer staging = CreateUploadBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, STAGING_MEMORY_SIZE);

    VkCommandBuffer cmd = BeginSingleTimeCommands(context); 
    {
        uint32 offset = 0;
        for (command_type *header = NextRenderCommand(commands, &offset); header; header = NextRenderCommand(commands, &offset))
        {
            if (*header == Load_Mesh)
            {
                command_load_mesh *entry = (command_load_mesh *)header;

                shared_alloc vertices = SharedBufferWrite(&res->VertexBuffer, entry->Vertices, entry->VertexCount * sizeof(vertex), 4);
                shared_alloc indices  = SharedBufferWrite(&res->IndexBuffer,  entry->Indices,  entry->IndexCount  * sizeof(uint32), sizeof(uint32));

                Assert(entry->MeshHandle < MAX_MESHES);

                gpu_mesh *mesh = res->Meshes + entry->MeshHandle;
                Assert(!mesh->IndexCount);

                *mesh = CreateMesh(vertices.Offset, entry->VertexCount, indices.Offset, entry->IndexCount);
            }
            else if (*header == Load_Texture)
            {
                command_load_texture *entry = (command_load_texture *)header;

                VkDeviceSize imageSize = (VkDeviceSize)entry->Width * entry->Height * TextureFormatBytes(entry->Format);

                shared_alloc upload  = SharedBufferWrite(&staging, entry->Pixels, imageSize, 16);
                gpu_texture *texture = CreateTexture(context, res, entry->TextureHandle, entry->Width, entry->Height, entry->SRGB, entry->Format);

                CmdUploadImage(cmd, staging.Buffer, upload.Offset, texture->Image, entry->Width, entry->Height, 1);
                WriteImageDescriptor(context, &res->Heap, res->Heap.TextureOffset, entry->TextureHandle, texture->View, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
            }
            else if (*header == Load_Cubemap)
            {
                command_load_cubemap *entry = (command_load_cubemap *)header;

                VkDeviceSize imageSize = (VkDeviceSize)entry->FaceSize * entry->FaceSize * 6 * TextureFormatBytes(entry->Format);

                shared_alloc upload = SharedBufferWrite(&staging, entry->Pixels, imageSize, 16);
                gpu_texture *cube = CreateCubemap(context, res, entry->CubemapHandle, entry->FaceSize, entry->Format);

                CmdUploadImage(cmd, staging.Buffer, upload.Offset, cube->Image, entry->FaceSize, entry->FaceSize, 6);
                CmdGenerateMips(cmd, cube->Image, entry->FaceSize, entry->FaceSize, 6, cube->MipLevels);
                WriteImageDescriptor(context, &res->Heap, res->Heap.CubemapOffset, entry->CubemapHandle, cube->View, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
            }
        }

        offset = 0;
        for (command_type *header = NextRenderCommand(commands, &offset); header; header = NextRenderCommand(commands, &offset))
        {
            if (*header == Load_Material)
            {
                command_load_material *entry = (command_load_material *)header;

                Assert(entry->TextureHandle < MAX_TEXTURES && res->Textures[entry->TextureHandle].View);
                Assert(entry->MaterialHandle < res->MaterialCount);

                material_state *state    = res->MaterialStates + entry->MaterialHandle;
                shared_alloc material = GetSharedBufferSlot(&res->MaterialBuffer, entry->MaterialHandle, sizeof(gpu_material));

                *state = CreateMaterialState(entry);
                *(gpu_material *)material.Cpu = CreateMaterial(entry);
            }
        }
    }
    EndSingleTimeCommands(context, cmd);

    DestroySharedBuffer(context, &staging);

    commands->LoadCount = 0;
}

internal void FillFrameGlobals(vulkan_context *context, vulkan_resources *res, render_commands *commands)
{
    frame_globals *globals = (frame_globals *)res->Globals.Cpu;

    real32 FOVaspect = (real32)context->swapchainExtent.width / (real32)context->swapchainExtent.height;

    globals->ScreenWidth  = context->swapchainExtent.width;
    globals->ScreenHeight = context->swapchainExtent.height;

    uint32 offset = 0;
    for (command_type *cmdBase = NextRenderCommand(commands, &offset); cmdBase; cmdBase = NextRenderCommand(commands, &offset))
    {
        switch (*cmdBase)
        {
            case Render_Light:
            {
                command_render_light *lightCmd = (command_render_light *)cmdBase;

                globals->LightDir   = lightCmd->Direction;
                globals->LightColor = lightCmd->Color;
            } break;

            case Render_Camera:
            {
                command_render_camera *cameraCmd = (command_render_camera *)cmdBase;

                Vector3 origin = cameraCmd->WorldPosition - cameraCmd->Position;

                globals->VolumeCenter = Vector3(0.0f, 0.0f, 0.0f) - origin;

                real32 nearPlane = 0.1f;
                real32 farPlane  = 100.0f;

                Matrix4 proj = Mat4Perspective(cameraCmd->FovY, FOVaspect, nearPlane, farPlane);

                globals->CameraNear = nearPlane;
                globals->CameraFar  = farPlane;

                globals->ViewProj  = Mat4Multiply(proj, cameraCmd->View);
                globals->CameraPos = cameraCmd->Position;

                Matrix4 *view = &cameraCmd->View;
                real32 rightScale = 1.0f / proj.Elements[0][0];
                real32 upScale    = 1.0f / proj.Elements[1][1];

                globals->SkyRight   = Vector4(view->Elements[0][0] * rightScale, view->Elements[1][0] * rightScale, view->Elements[2][0] * rightScale, 0.0f);
                globals->SkyUp      = Vector4(view->Elements[0][1] * upScale,    view->Elements[1][1] * upScale,    view->Elements[2][1] * upScale,    0.0f);
                globals->SkyForward = Vector4(-view->Elements[0][2], -view->Elements[1][2], -view->Elements[2][2], 0.0f);
            } break;

            case Render_Skybox:
            {
                command_render_skybox *skyCmd = (command_render_skybox *)cmdBase;

                uint32 cubeSlot = skyCmd->CubemapHandle;
                Assert(cubeSlot < MAX_CUBEMAPS);
                Assert(res->Cubemaps[cubeSlot].View);

                globals->SkyCubemap  = cubeSlot;
                globals->SkyMipCount = res->Cubemaps[cubeSlot].MipLevels;
            } break;
        }
    }
}

internal void ExecuteRenderCommands(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, render_pipeline *pipelines, render_commands *commands)
{
    render_pipeline *pipeline = 0;
    uint32 piplineId = Pipeline_Count;

    render_state current = {};
    render_state wanted  = {};

    vkCmdBindIndexBuffer(cmd, res->IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

    for (uint32 queue = 0; queue < Queue_Count; ++queue)
    {
        uint32 offset   = 0;
        for (command_type *cmdBase = NextRenderCommand(commands, &offset); cmdBase; cmdBase = NextRenderCommand(commands, &offset))
        {
            if (*cmdBase != Render_Mesh && queue != Queue_Opaque)
            {
                continue;
            }

            switch (*cmdBase)
            {
                case Render_Skybox:
                {
                    command_render_skybox *skyCmd = (command_render_skybox *)cmdBase;

                    uint32 cubeSlot = skyCmd->CubemapHandle;
                    Assert(cubeSlot < MAX_CUBEMAPS);

                    piplineId = Pipeline_Skybox;
                    pipeline = &pipelines[piplineId];

                    BindPipelineState(context, cmd, pipeline, &current, &wanted);
                    ApplyRenderState(context, cmd, &current, &wanted);

                    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(skybox_params), 16);

                    skybox_params params = {};
                    params.Tint         = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                    params.CubemapIndex = cubeSlot;

                    *(skybox_params *)alloc.Cpu = params;

                    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

                    vkCmdDraw(cmd, 3, 1, 0, 0);
                } break;

                case Render_Mesh:
                {
                    command_render_mesh *meshCmd = (command_render_mesh *)cmdBase;
                    Assert(meshCmd->MeshHandle < MAX_MESHES);

                    gpu_mesh *mesh = res->Meshes + meshCmd->MeshHandle;
                    Assert(mesh->IndexCount);

                    uint32 materialSlot = meshCmd->MaterialHandle;
                    Assert(materialSlot < res->MaterialCount);

                    material_state *material = &res->MaterialStates[materialSlot];

                    if ((uint32)material->Queue != queue)
                    {
                        break;
                    }

                    if ((uint32)material->Pipeline != piplineId)
                    {
                        if (pipelines[material->Pipeline].Vert == VK_NULL_HANDLE)
                        {
                            break;
                        }

                        piplineId = (uint32)material->Pipeline;
                        pipeline = &pipelines[piplineId];

                        BindPipelineState(context, cmd, pipeline, &current, &wanted);
                    }

                    wanted.CullMode   = ToVulkanCullMode(material->CullMode);
                    wanted.DepthTest  = material->DepthTest  ? VK_TRUE : VK_FALSE;
                    wanted.DepthWrite = material->DepthWrite ? VK_TRUE : VK_FALSE;
                    wanted.AlphaBlend = (material->BlendMode == Blend_Alpha) ? VK_TRUE : VK_FALSE;

                    ApplyRenderState(context, cmd, &current, &wanted);

                    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(draw_params), 16);

                    draw_params params = {};
                    params.Model         = meshCmd->Transform;
                    params.Tint          = meshCmd->Tint;
                    params.Vertices      = res->VertexBuffer.Address;
                    params.Materials     = res->MaterialBuffer.Address;
                    params.MaterialSlot  = materialSlot;

                    *(draw_params *)alloc.Cpu = params;

                    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

                    vkCmdDrawIndexed(cmd, mesh->IndexCount, 1, mesh->FirstIndex, (int32)mesh->FirstVertex, 0);
                } break;
            }
        }
    }
}

internal void ExecuteUICommands(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, render_pipeline *pipelines, render_commands *commands)
{
    render_pipeline *pipeline = &pipelines[Pipeline_UIRect];
    if (pipeline->Vert == VK_NULL_HANDLE)
    {
        return;
    }

    uint32 rectCount = commands->RectCount;
    if (!rectCount)
    {
        return;
    }

    real32 width  = (real32)context->swapchainExtent.width;
    real32 height = (real32)context->swapchainExtent.height;

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, rectCount * sizeof(rect_params), 16);

    rect_params *params = (rect_params *)alloc.Cpu;
    uint32       index  = 0;

    uint32 offset = 0;
    for (command_type *cmdBase = NextRenderCommand(commands, &offset); cmdBase; cmdBase = NextRenderCommand(commands, &offset))
    {
        if (*cmdBase != Render_Rect)
        {
            continue;
        }

        command_render_rect *rectCmd = (command_render_rect *)cmdBase;

        rect_params entry = {};
        entry.Rect = Vector4(rectCmd->Min.X / width  * 2.0f - 1.0f, rectCmd->Min.Y / height * 2.0f - 1.0f, rectCmd->Max.X / width  * 2.0f - 1.0f, rectCmd->Max.Y / height * 2.0f - 1.0f);
        entry.UVRect = rectCmd->UV;
        entry.Tint = rectCmd->Color;
        entry.TextureSlot = rectCmd->TextureSlot;

        params[index++] = entry;
    }

    render_state current = {};
    render_state wanted  = {};
    BindPipelineState(context, cmd, pipeline, &current, &wanted);

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    vkCmdDraw(cmd, 6, rectCount, 0, 0);
}

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

internal void StorageBarrier(VkCommandBuffer cmd)
{
    GpuBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

internal void ComputeSkyOcclusion(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(downsample_params), 16);

    downsample_params params = {};
    params.AlbedoSlot = VOLUME_SLOT_ALBEDO;
    params.NormalSlot = VOLUME_SLOT_SKY_OCCLUSION;
    params.VoxelSize  = VOLUME_GRID_SIZE;

    *(downsample_params *)alloc.Cpu = params;

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

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(downsample_params), 16);

    downsample_params params = {};
    params.AlbedoSlot = sourceSlot;
    params.NormalSlot = targetSlot;
    params.VoxelSize  = VOLUME_GRID_SIZE;

    *(downsample_params *)alloc.Cpu = params;

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

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(downsample_params), 16);

    downsample_params params = {};
    params.AlbedoSlot = VOLUME_SLOT_RADIANCE;
    params.NormalSlot = VOLUME_SLOT_RADIANCE_SMOOTH;
    params.VoxelSize  = LIGHT_GRID_SIZE;

    *(downsample_params *)alloc.Cpu = params;

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

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(downsample_params), 16);

    downsample_params params = {};
    params.AlbedoSlot      = VOLUME_SLOT_CASCADE + RC_SCREEN_HANDOFF + 1;
    params.NormalSlot      = VOLUME_SLOT_HANDOFF;
    params.SolidSlot       = RC_DIR_RES << (RC_SCREEN_HANDOFF + 1);
    params.LightNormalSlot = RC_SCREEN_DIR_RES;
    params.VoxelSize       = RC_PROBE_SIZE >> (RC_SCREEN_HANDOFF + 1);

    *(downsample_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 tile = params.VoxelSize * RC_SCREEN_DIR_RES;

    uint32 groupCount = (tile + RC_GROUP_SIZE - 1) / RC_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, params.VoxelSize);
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

internal void RenderDepthPrepass(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, render_pipeline *pipeline, render_commands *commands)
{
    if (pipeline->Vert == VK_NULL_HANDLE)
    {
        return;
    }

    render_state current = {};
    render_state wanted  = {};

    BindPipelineState(context, cmd, pipeline, &current, &wanted);

    vkCmdBindIndexBuffer(cmd, res->IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

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
        if (material->Queue != Queue_Opaque || !material->DepthWrite)
        {
            continue;
        }

        wanted.CullMode = ToVulkanCullMode(material->CullMode);

        ApplyRenderState(context, cmd, &current, &wanted);

        shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(draw_params), 16);

        draw_params params = {};
        params.Model        = meshCmd->Transform;
        params.Tint         = meshCmd->Tint;
        params.Vertices     = res->VertexBuffer.Address;
        params.Materials    = res->MaterialBuffer.Address;
        params.MaterialSlot = materialSlot;

        *(draw_params *)alloc.Cpu = params;

        BindParams(cmd, res->PipelineLayout, alloc.Gpu);

        vkCmdDrawIndexed(cmd, mesh->IndexCount, 1, mesh->FirstIndex, (int32)mesh->FirstVertex, 0);
    }
}

internal void VoxelizeScene(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipelines, render_commands *commands)
{
    compute_pipeline *clear     = &pipelines[Compute_VolumeClear];
    compute_pipeline *uintClear = &pipelines[Compute_UintClear];
    compute_pipeline *voxelize  = &pipelines[Compute_Voxelize];
    compute_pipeline *resolve   = &pipelines[Compute_VoxelResolve];

    if (clear->Compute == VK_NULL_HANDLE || uintClear->Compute == VK_NULL_HANDLE || voxelize->Compute == VK_NULL_HANDLE || resolve->Compute == VK_NULL_HANDLE)
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

    frame_globals *globals = (frame_globals *)res->Globals.Cpu;

    BindComputePipeline(context, cmd, voxelize);

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

    StorageBarrier(cmd);

    BindComputePipeline(context, cmd, resolve);

    shared_alloc resolveAlloc = SharedBufferAlloc(&res->FrameArena, sizeof(downsample_params), 16);

    downsample_params resolveParams = {};
    resolveParams.AlbedoSlot = VOLUME_SLOT_ALBEDO;
    resolveParams.NormalSlot = VOLUME_SLOT_NORMAL;
    resolveParams.VoxelSize  = VOLUME_GRID_SIZE;

    *(downsample_params *)resolveAlloc.Cpu = resolveParams;

    BindParams(cmd, res->PipelineLayout, resolveAlloc.Gpu);

    DispatchCompute(cmd, voxelGroups, voxelGroups, voxelGroups);
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

internal void RenderVulkanFrame(render_commands *Commands)
{
    vulkan_context *context = &GlobalVulkan;

    if (Pipelines[Pipeline_Unlit].Vert == VK_NULL_HANDLE)
    {
        return;
    }

    WaitForFrame(context);

    LoadAssets(context, &GlobalResources, Commands);

    vulkan_frame Frame = BeginFrame(context, &GlobalResources);
    if (!Frame.Ready)
    {
        if (Frame.NeedsResize)
        {
            ResizeRenderer(context);
        }
        return;
    }

    VkImage swapchainImage = context->swapchainImages[Frame.ImageIndex];

    FillFrameGlobals(context, &GlobalResources, Commands);

    BindDescriptorHeap(context, Frame.Cmd, &GlobalResources, GlobalResources.PipelineLayout);

    uint32 stampBase = Frame.Slot * 16;

    if (TimestampReady[Frame.Slot])
    {
        uint64 stamps[9];

        if (vkGetQueryPoolResults(context->device, context->timestampPool, stampBase, 9, sizeof(stamps), stamps, sizeof(uint64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
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
    }

    vkCmdResetQueryPool(Frame.Cmd, context->timestampPool, stampBase, 16);

    GpuStamp(context, Frame.Cmd, stampBase, 0);

    VoxelizeScene(context, Frame.Cmd, &GlobalResources, ComputePipelines, Commands);

    GpuStamp(context, Frame.Cmd, stampBase, 1);

    StorageBarrier(Frame.Cmd);

    ComputeSkyOcclusion(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_SkyOcclusion]);

    StorageBarrier(Frame.Cmd);

    BlurSkyOcclusion(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_SkyOcclusionBlur], VOLUME_SLOT_SKY_OCCLUSION, VOLUME_SLOT_SKY_OCCLUSION_SCRATCH);

    StorageBarrier(Frame.Cmd);

    BlurSkyOcclusion(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_SkyOcclusionBlur], VOLUME_SLOT_SKY_OCCLUSION_SCRATCH, VOLUME_SLOT_SKY_OCCLUSION);

    GpuStamp(context, Frame.Cmd, stampBase, 2);

    StorageBarrier(Frame.Cmd);

    InjectRadiance(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_RcInject]);

    StorageBarrier(Frame.Cmd);

    SmoothRadiance(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_RcSmooth]);

    GpuStamp(context, Frame.Cmd, stampBase, 3);

    StorageBarrier(Frame.Cmd);

    TraceCascades(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_RcTrace]);

    MergeCascades(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_RcMerge]);

    StorageBarrier(Frame.Cmd);

    ResolveIrradiance(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_RcResolve]);

    PrefilterHandoff(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_RcPrefilter]);

    GpuStamp(context, Frame.Cmd, stampBase, 4);

    StorageBarrier(Frame.Cmd);

    BeginPass(context, Frame.Cmd, SceneTarget.View, VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), Depth_Clear);
    RenderDepthPrepass(context, Frame.Cmd, &GlobalResources, &Pipelines[Pipeline_Depth], Commands);
    EndPass(Frame.Cmd);

    GpuStamp(context, Frame.Cmd, stampBase, 5);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    ComputeScreenGI(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_RcScreen]);

    GpuStamp(context, Frame.Cmd, stampBase, 6);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    BeginPass(context, Frame.Cmd, SceneTarget.View, VK_ATTACHMENT_LOAD_OP_CLEAR, Vector4(0.05f, 0.05f, 0.08f, 1.0f), Depth_Load);
    ExecuteRenderCommands(context, Frame.Cmd, &GlobalResources, Pipelines, Commands);
    EndPass(Frame.Cmd);

    GpuStamp(context, Frame.Cmd, stampBase, 7);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    BeginPass(context, Frame.Cmd, PostTarget.View, VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), Depth_None);
    DrawFullscreen(context, Frame.Cmd, &GlobalResources, &Pipelines[Pipeline_Post], TEXTURE_SLOT_SCENE);
    EndPass(Frame.Cmd);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    CmdImageToGeneral(Frame.Cmd, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    BeginPass(context, Frame.Cmd, context->swapchainImageViews[Frame.ImageIndex], VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), Depth_None);
    DrawFullscreen(context, Frame.Cmd, &GlobalResources, &Pipelines[Pipeline_UI], TEXTURE_SLOT_POST);
    if (Commands->ShowVolumeDebug)
    {
        DrawVolumeDebug(context, Frame.Cmd, &GlobalResources, &Pipelines[Pipeline_VolumeView], VOLUME_SLOT_ALBEDO, VOLUME_GRID_SIZE, VOLUME_MODE_LIGHT, 0.5f);
    }

    ExecuteUICommands(context, Frame.Cmd, &GlobalResources, Pipelines, Commands);
    EndPass(Frame.Cmd);

    GpuStamp(context, Frame.Cmd, stampBase, 8);

    TimestampReady[Frame.Slot] = true;

    CmdImageToPresent(Frame.Cmd, swapchainImage);

    EndFrame(context, &Frame);

    if (Frame.NeedsResize)
    {
        ResizeRenderer(context);
    }
}
