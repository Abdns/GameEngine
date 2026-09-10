#include "Vulkan.h"

#define STAGING_MEMORY_SIZE   Megabytes(64)

#include "VulkanDebug.cpp"
#include "VulkanCore.cpp"
#include "VulkanDevice.cpp"
#include "VulkanResources.cpp"
#include "VulkanPasses.cpp"
#include "VulkanPipeline.cpp"
#include "VulkanFrame.cpp"

global_variable vulkan_renderer GlobalRenderer;

internal void ResizeRenderer(vulkan_renderer *renderer)
{
    vulkan_context   *context = &renderer->Context;
    vulkan_resources *res     = &renderer->Resources;
    frame_targets    *targets = &renderer->Targets;

    if (!RecreateSwapchain(context))
    {
        return;
    }

    DestroyFrameTargets(context, targets);

    VkCommandBuffer setup = BeginSingleTimeCommands(context);
    {
        *targets = CreateFrameTargets(context, &res->Heap, setup);
    }
    EndSingleTimeCommands(context, setup);
}

internal const char *InitVulkan(HINSTANCE hinstance, HWND hwnd)
{
    vulkan_renderer *renderer = &GlobalRenderer;
    vulkan_context  *context  = &renderer->Context;
    vulkan_resources *res     = &renderer->Resources;
    frame_targets    *targets = &renderer->Targets;

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

    *res = CreateResources(context);

    VkCommandBuffer setup = BeginSingleTimeCommands(context);
    {
        *targets = CreateFrameTargets(context, &res->Heap, setup);
    }
    EndSingleTimeCommands(context, setup);

    renderer->Pipelines = CreatePipelines(context, res);

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

    res->VertexBuffer   = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(vertex) * commands->VertexCount);
    res->IndexBuffer    = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,   sizeof(uint32) * commands->IndexCount);
    res->MaterialBuffer = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(gpu_material) * commands->MaterialCount);
    res->MaterialCount  = commands->MaterialCount;

    gpu_buffer staging = CreateBuffer(context, Buffer_Upload, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, STAGING_MEMORY_SIZE);

    VkCommandBuffer cmd = BeginSingleTimeCommands(context); 
    {
        uint32 offset = 0;
        for (command_type *header = NextRenderCommand(commands, &offset); header; header = NextRenderCommand(commands, &offset))
        {
            if (*header == Load_Mesh)
            {
                command_load_mesh *entry = (command_load_mesh *)header;

                gpu_alloc vertices = BufferWrite(&res->VertexBuffer, entry->Vertices, entry->VertexCount * sizeof(vertex), 4);
                gpu_alloc indices  = BufferWrite(&res->IndexBuffer,  entry->Indices,  entry->IndexCount  * sizeof(uint32), sizeof(uint32));

                Assert(entry->MeshHandle < MAX_MESHES);

                gpu_mesh *mesh = res->Meshes + entry->MeshHandle;
                Assert(!mesh->IndexCount);

                *mesh = CreateMesh(vertices.Offset, entry->VertexCount, indices.Offset, entry->IndexCount);
            }
            else if (*header == Load_Texture)
            {
                command_load_texture *entry = (command_load_texture *)header;

                VkDeviceSize imageSize = (VkDeviceSize)entry->Width * entry->Height * TextureFormatBytes(entry->Format);

                gpu_alloc upload  = BufferWrite(&staging, entry->Pixels, imageSize, 16);
                gpu_image *texture = CreateTexture(context, res, entry->TextureHandle, entry->Width, entry->Height, entry->SRGB, entry->Format);

                CmdUploadImage(cmd, staging.Buffer, upload.Offset, texture->Image, entry->Width, entry->Height, 1);
                WriteHeapImage(context, &res->Heap, BINDING_TEXTURES, entry->TextureHandle, texture->View);
            }
            else if (*header == Load_Cubemap)
            {
                command_load_cubemap *entry = (command_load_cubemap *)header;

                VkDeviceSize imageSize = (VkDeviceSize)entry->FaceSize * entry->FaceSize * 6 * TextureFormatBytes(entry->Format);

                gpu_alloc upload = BufferWrite(&staging, entry->Pixels, imageSize, 16);
                gpu_image *cube = CreateCubemap(context, res, entry->CubemapHandle, entry->FaceSize, entry->Format);

                CmdUploadImage(cmd, staging.Buffer, upload.Offset, cube->Image, entry->FaceSize, entry->FaceSize, 6);
                CmdGenerateMips(cmd, cube->Image, entry->FaceSize, entry->FaceSize, 6, cube->MipLevels);
                WriteHeapImage(context, &res->Heap, BINDING_CUBEMAPS, entry->CubemapHandle, cube->View);
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
                gpu_alloc material = BufferSlot(&res->MaterialBuffer, entry->MaterialHandle, sizeof(gpu_material));

                *state = CreateMaterialState(entry);
                *(gpu_material *)material.Cpu = CreateMaterial(entry);
            }
        }
    }
    EndSingleTimeCommands(context, cmd);

    DestroyBuffer(context, &staging);

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

                real32 nearPlane = 0.1f;
                real32 farPlane  = 100.0f;

                Matrix4 proj = Mat4Perspective(cameraCmd->FovY, FOVaspect, nearPlane, farPlane);

                globals->ViewProj  = Mat4Multiply(proj, cameraCmd->View);
                globals->CameraPos = cameraCmd->Position;

                Matrix4 *view = &cameraCmd->View;
                real32 rightScale = 1.0f / proj.Elements[0][0];
                real32 upScale    = 1.0f / proj.Elements[1][1];

                globals->SkyRight   = Vector4(view->Elements[0][0] * rightScale, view->Elements[1][0] * rightScale, view->Elements[2][0] * rightScale, 0.0f);
                globals->SkyUp      = Vector4(view->Elements[0][1] * upScale,    view->Elements[1][1] * upScale,    view->Elements[2][1] * upScale,    0.0f);
                globals->SkyForward = Vector4(-view->Elements[0][2], -view->Elements[1][2], -view->Elements[2][2], 0.0f);
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

                    skybox_params params = {};
                    params.Tint         = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                    params.CubemapIndex = cubeSlot;

                    PushPassParams(cmd, res, params);

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

                    draw_params params = {};
                    params.Model         = meshCmd->Transform;
                    params.Tint          = meshCmd->Tint;
                    params.Vertices      = res->VertexBuffer.Address;
                    params.Materials     = res->MaterialBuffer.Address;
                    params.MaterialSlot  = materialSlot;

                    PushPassParams(cmd, res, params);

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

    gpu_alloc alloc = BufferAlloc(&res->FrameArena, rectCount * sizeof(rect_params), 16);

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

internal void RenderVulkanFrame(render_commands *Commands)
{
    vulkan_renderer *renderer  = &GlobalRenderer;
    vulkan_context  *context   = &renderer->Context;
    vulkan_resources *res      = &renderer->Resources;
    frame_targets    *targets  = &renderer->Targets;
    vulkan_pipelines *pipelines = &renderer->Pipelines;

    if (pipelines->Render[Pipeline_Unlit].Vert == VK_NULL_HANDLE)
    {
        return;
    }

    WaitForFrame(context);

    LoadAssets(context, res, Commands);

    vulkan_frame Frame = BeginFrame(context, res);
    if (!Frame.Ready)
    {
        if (Frame.NeedsResize)
        {
            ResizeRenderer(renderer);
        }
        return;
    }

    VkImage swapchainImage = context->swapchainImages[Frame.ImageIndex];

    FillFrameGlobals(context, res, Commands);
    BindDescriptorHeap(context, Frame.Cmd, res, res->PipelineLayout);

    BeginGpuFrame(context, &Frame);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    {
        GpuSection(context, &Frame, "scene");

        BeginPass(context, Frame.Cmd, targets->Scene.View, targets->Depth.View, VK_ATTACHMENT_LOAD_OP_CLEAR, Vector4(0.05f, 0.05f, 0.08f, 1.0f), Depth_Clear);
        ExecuteRenderCommands(context, Frame.Cmd, res, pipelines->Render, Commands);
        EndPass(Frame.Cmd);
    }

    {
        GpuSection(context, &Frame, "post");

        GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        BeginPass(context, Frame.Cmd, targets->Post.View, VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), Depth_None);
        DrawFullscreen(context, Frame.Cmd, res, &pipelines->Render[Pipeline_Post], TEXTURE_SLOT_SCENE);
        EndPass(Frame.Cmd);
        GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        CmdAcquiredImageToGeneral(Frame.Cmd, swapchainImage);

        BeginPass(context, Frame.Cmd, context->swapchainImageViews[Frame.ImageIndex], VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), Depth_None);
        DrawFullscreen(context, Frame.Cmd, res, &pipelines->Render[Pipeline_UI], TEXTURE_SLOT_POST);
        ExecuteUICommands(context, Frame.Cmd, res, pipelines->Render, Commands);
        EndPass(Frame.Cmd);
    }

    EndGpuFrame(&Frame);

    CmdImageToPresent(Frame.Cmd, swapchainImage);

    EndFrame(context, &Frame);

    if (Frame.NeedsResize)
    {
        ResizeRenderer(renderer);
    }
}
