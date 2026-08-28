#include "Vulkan.h"

#include "VulkanCore.cpp"
#include "VulkanDevice.cpp"
#include "VulkanResources.cpp"
#include "VulkanPasses.cpp"
#include "VulkanPipeline.cpp"
#include "VulkanFrame.cpp"

global_variable render_pipeline  Pipelines[Pipeline_Count];
global_variable compute_pipeline ComputePipelines[Compute_Count];

global_variable bool32           LightHistoryReady;

global_variable pipeline_desc PipelineDescs[] =
{
    { "unlit",      VK_TRUE,  VK_TRUE,  VK_FALSE },
    { "lit",        VK_TRUE,  VK_TRUE,  VK_FALSE },
    { "skybox",     VK_FALSE, VK_FALSE, VK_FALSE },
    { "post",       VK_FALSE, VK_FALSE, VK_FALSE },
    { "UI",         VK_FALSE, VK_FALSE, VK_FALSE },
    { "uirect",     VK_FALSE, VK_FALSE, VK_TRUE  },
    { "volumeview", VK_FALSE, VK_FALSE, VK_FALSE },
};

global_variable const char *ComputeDescs[] =
{
    "voxelclear",
    "voxelize",
    "lightdownsample",
    "lightinject",
    "lightpropagate",
    "lightblend",
    "uintclear",
    "voxelresolve",
    "skyocclusion",
    "skyocclusionblur",
};

static_assert(ArrayCount(PipelineDescs) == Pipeline_Count, "PipelineDescs must describe every pipeline_type");
static_assert(ArrayCount(ComputeDescs) == Compute_Count, "ComputeDescs must describe every compute_type");

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
        CreateDepthResources(context, setup);

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

    CreateSwapchain(context, hwnd);
    CreateSwapchainImageViews(context);

    VkCommandBuffer setup = BeginSingleTimeCommands(context);
    {
        CreateResources(context, &GlobalResources, setup);
        CreateDepthResources(context, setup);

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

                Matrix4 proj = Mat4Perspective(cameraCmd->FovY, FOVaspect, 0.1f, 100.0f);

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

internal void DownsampleLightGrid(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(downsample_params), 16);

    downsample_params params = {};
    params.AlbedoSlot      = VOLUME_SLOT_ALBEDO;
    params.NormalSlot      = VOLUME_SLOT_NORMAL;
    params.SolidSlot       = VOLUME_SLOT_LIGHT_SOLID;
    params.LightNormalSlot = VOLUME_SLOT_LIGHT_NORMAL;
    params.VoxelSize       = VOLUME_GRID_SIZE;
    params.LightSize       = LIGHT_GRID_SIZE;

    *(downsample_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
}

internal void InjectDirectLight(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(inject_params), 16);

    inject_params params = {};
    params.SolidSlot  = VOLUME_SLOT_LIGHT_SOLID;
    params.NormalSlot = VOLUME_SLOT_LIGHT_NORMAL;
    params.LightSlot  = VOLUME_SLOT_LIGHT_A;
    params.LightSize  = LIGHT_GRID_SIZE;
    params.SumSlot    = VOLUME_SLOT_LIGHT_SUM;

    *(inject_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
}

internal void PropagateLight(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    uint32 source = VOLUME_SLOT_LIGHT_A;
    uint32 target = VOLUME_SLOT_LIGHT_B;

    for (uint32 iteration = 0; iteration < LIGHT_ITERATIONS; ++iteration)
    {
        StorageBarrier(cmd);

        shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(propagate_params), 16);

        propagate_params params = {};
        params.SourceSlot = source;
        params.TargetSlot = target;
        params.SumSlot    = VOLUME_SLOT_LIGHT_SUM;
        params.SolidSlot  = VOLUME_SLOT_LIGHT_SOLID;
        params.LightSize  = LIGHT_GRID_SIZE;

        *(propagate_params *)alloc.Cpu = params;

        BindParams(cmd, res->PipelineLayout, alloc.Gpu);

        DispatchCompute(cmd, groupCount, groupCount, groupCount);

        uint32 swap = source;
        source      = target;
        target      = swap;
    }
}

internal void BlendLightHistory(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, compute_pipeline *pipeline)
{
    if (pipeline->Compute == VK_NULL_HANDLE)
    {
        return;
    }

    BindComputePipeline(context, cmd, pipeline);

    shared_alloc alloc = SharedBufferAlloc(&res->FrameArena, sizeof(propagate_params), 16);

    propagate_params params = {};
    params.SourceSlot = VOLUME_SLOT_LIGHT_SUM;
    params.TargetSlot = VOLUME_SLOT_LIGHT_HISTORY;
    params.SumSlot    = VOLUME_SLOT_LIGHT_SUM;
    params.SolidSlot  = VOLUME_SLOT_LIGHT_SOLID;
    params.LightSize  = LIGHT_GRID_SIZE;

    *(propagate_params *)alloc.Cpu = params;

    BindParams(cmd, res->PipelineLayout, alloc.Gpu);

    uint32 groupCount = LIGHT_GRID_SIZE / VOLUME_GROUP_SIZE;

    DispatchCompute(cmd, groupCount, groupCount, groupCount);
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

    if (!LightHistoryReady)
    {
        BindComputePipeline(context, cmd, clear);

        for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
        {
            ClearVolume(cmd, res, VOLUME_SLOT_LIGHT_HISTORY + i, LIGHT_GRID_SIZE);
        }

        LightHistoryReady = true;
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
    params.VolumeLightSlot  = VOLUME_SLOT_LIGHT_HISTORY;

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

    VoxelizeScene(context, Frame.Cmd, &GlobalResources, ComputePipelines, Commands);

    StorageBarrier(Frame.Cmd);

    ComputeSkyOcclusion(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_SkyOcclusion]);

    StorageBarrier(Frame.Cmd);

    BlurSkyOcclusion(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_SkyOcclusionBlur], VOLUME_SLOT_SKY_OCCLUSION, VOLUME_SLOT_SKY_OCCLUSION_SCRATCH);

    StorageBarrier(Frame.Cmd);

    BlurSkyOcclusion(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_SkyOcclusionBlur], VOLUME_SLOT_SKY_OCCLUSION_SCRATCH, VOLUME_SLOT_SKY_OCCLUSION);

    StorageBarrier(Frame.Cmd);

    DownsampleLightGrid(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_LightDownsample]);

    StorageBarrier(Frame.Cmd);

    InjectDirectLight(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_LightInject]);

    PropagateLight(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_LightPropagate]);

    StorageBarrier(Frame.Cmd);

    BlendLightHistory(context, Frame.Cmd, &GlobalResources, &ComputePipelines[Compute_LightBlend]);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    BeginPass(context, Frame.Cmd, SceneTarget.View, VK_ATTACHMENT_LOAD_OP_CLEAR, Vector4(0.05f, 0.05f, 0.08f, 1.0f), true);
    ExecuteRenderCommands(context, Frame.Cmd, &GlobalResources, Pipelines, Commands);
    EndPass(Frame.Cmd);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    BeginPass(context, Frame.Cmd, PostTarget.View, VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), false);
    DrawFullscreen(context, Frame.Cmd, &GlobalResources, &Pipelines[Pipeline_Post], TEXTURE_SLOT_SCENE);
    EndPass(Frame.Cmd);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    CmdImageToGeneral(Frame.Cmd, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    BeginPass(context, Frame.Cmd, context->swapchainImageViews[Frame.ImageIndex], VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), false);
    DrawFullscreen(context, Frame.Cmd, &GlobalResources, &Pipelines[Pipeline_UI], TEXTURE_SLOT_POST);
    ExecuteUICommands(context, Frame.Cmd, &GlobalResources, Pipelines, Commands);
    DrawVolumeDebug(context, Frame.Cmd, &GlobalResources, &Pipelines[Pipeline_VolumeView], VOLUME_SLOT_ALBEDO, VOLUME_GRID_SIZE, VOLUME_MODE_LIGHT, 0.5f);
    EndPass(Frame.Cmd);

    CmdImageToPresent(Frame.Cmd, swapchainImage);

    EndFrame(context, &Frame);

    if (Frame.NeedsResize)
    {
        ResizeRenderer(context);
    }
}
