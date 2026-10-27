#include "Vulkan.h"

#define STAGING_MEMORY_SIZE   Megabytes(64)

#include "VulkanMemory.cpp"
#include "VulkanRender.h"
#include "VulkanDebug.cpp"
#include "VulkanCore.cpp"
#include "VulkanDevice.cpp"
#include "VulkanResources.cpp"
#include "VulkanPasses.cpp"
#include "VulkanPipeline.cpp"
#include "VulkanFrame.cpp"
#include "Memory.h"

static_assert(CommandCapacity(Load_Mesh) == MAX_MESHES, "Mesh load capacity must match mesh storage");
static_assert(CommandCapacity(Load_Texture) == MAX_TEXTURES, "Texture load capacity must match texture storage");
static_assert(CommandCapacity(Load_Cubemap) == MAX_CUBEMAPS, "Cubemap load capacity must match cubemap storage");
static_assert(CommandCapacity(Load_Material) == MAX_MATERIALS, "Material load capacity must match material storage");


internal void ResizeRenderer(vulkan_context *context, vulkan_resources *res, frame_targets *targets)
{
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

internal vulkan_resources CreateResources(vulkan_context* context)
{
    vulkan_resources res = {};

    for (uint32 slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot)
    {
        res.FrameBuffers[slot] = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, FRAME_BUFFER_SIZE);
    }
    res.GlobalsBuffer = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(frame_globals) * MAX_FRAMES_IN_FLIGHT);
    res.Sampler = CreateTextureSampler(context, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    res.Heap = CreateDescriptorHeap(context);
    res.PipelineLayout = CreatePipelineLayout(context, res.Heap.Layout);

    WriteHeapSampler(context, &res.Heap, BINDING_SAMPLER, 0, res.Sampler);

    return res;
}

internal void InitVulkan(vulkan_context *context, vulkan_resources *res, frame_targets *targets, vulkan_pipelines *pipelines, HINSTANCE hinstance, HWND hwnd)
{
    context->windowHandle = hwnd;

    CreateInstance(context);

    CreateSurface(context, hinstance, hwnd);
    SelectDevice(context);

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

    *pipelines = CreatePipelines(context, res);
}

internal void LoadAssets(vulkan_context *context, vulkan_resources *res, render_commands *commands)
{
    if (!commands->CommandCounts[Load_Mesh] &&
        !commands->CommandCounts[Load_Texture] &&
        !commands->CommandCounts[Load_Cubemap] &&
        !commands->CommandCounts[Load_Material])
    {
        return;
    }

    Assert(res->VertexBuffer.Buffer == VK_NULL_HANDLE);
    Assert(commands->VertexCount && commands->IndexCount);
    Assert(commands->MaterialCount && commands->MaterialCount <= MAX_MATERIALS);

    res->VertexBuffer   = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(vertex) * commands->VertexCount);
    res->IndexBuffer    = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,   sizeof(uint32) * commands->IndexCount);
    res->MaterialBuffer = CreateBuffer(context, Buffer_GpuShared, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(gpu_material) * commands->MaterialCount);

    gpu_buffer staging = CreateBuffer(context, Buffer_Upload, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, STAGING_MEMORY_SIZE);

    VkCommandBuffer cmd = BeginSingleTimeCommands(context); 
    {
        for (uint32 Type = Load_Mesh; Type <= Load_Material; ++Type)
        {
            for (uint32 Index = 0; Index < commands->CommandCounts[Type]; ++Index)
            {
                command_type *header = commands->Commands[Type][Index];
                if (*header == Load_Mesh)
                {
                    command_load_mesh *entry = (command_load_mesh *)header;

                    gpu_buffer_slice vertices = BufferWrite(&res->VertexBuffer, entry->Vertices, entry->VertexCount * sizeof(vertex), 4);
                    gpu_buffer_slice indices  = BufferWrite(&res->IndexBuffer,  entry->Indices,  entry->IndexCount  * sizeof(uint32), sizeof(uint32));

                    Assert(entry->MeshHandle < MAX_MESHES);

                    gpu_mesh *mesh = &res->Meshes[entry->MeshHandle];
                    Assert(!mesh->IndexCount);

                    *mesh = CreateMesh(vertices.Offset, entry->VertexCount, indices.Offset, entry->IndexCount);
                }
                else if (*header == Load_Texture)
                {
                    command_load_texture *entry = (command_load_texture *)header;

                    VkDeviceSize imageSize = (VkDeviceSize)entry->Width * entry->Height * ImageFormatBytes(entry->Format);

                    gpu_buffer_slice upload = BufferWrite(&staging, entry->Pixels, imageSize, 16);
                    gpu_image *texture = CreateTexture(context, res, entry->TextureHandle, entry->Width, entry->Height, entry->SRGB, entry->Format);

                    CmdUploadImage(cmd, staging.Buffer, upload.Offset, texture->Image, entry->Width, entry->Height, 1);
                    WriteHeapImage(context, &res->Heap, BINDING_TEXTURES, entry->TextureHandle, texture->View);
                }
                else if (*header == Load_Cubemap)
                {
                    command_load_cubemap *entry = (command_load_cubemap *)header;

                    VkDeviceSize imageSize = (VkDeviceSize)entry->FaceSize * entry->FaceSize * 6 * ImageFormatBytes(entry->Format);

                    gpu_buffer_slice upload = BufferWrite(&staging, entry->Pixels, imageSize, 16);
                    gpu_image *cube = CreateCubemap(context, res, entry->CubemapHandle, entry->FaceSize, entry->Format);

                    CmdUploadImage(cmd, staging.Buffer, upload.Offset, cube->Image, entry->FaceSize, entry->FaceSize, 6);
                    CmdGenerateMips(cmd, cube->Image, entry->FaceSize, entry->FaceSize, 6, cube->MipLevels);
                    WriteHeapImage(context, &res->Heap, BINDING_CUBEMAPS, entry->CubemapHandle, cube->View);
                }
                else if (*header == Load_Material)
                {
                    command_load_material *entry = (command_load_material*)header;
                    const material *materialDesc = &entry->Description;

                    Assert(materialDesc->Pipeline < Pipeline_MeshCount);
                    //Assert(description->TextureHandle < MAX_TEXTURES && res->Textures[description->TextureHandle].View);

                    Assert(res->MaterialBuffer.CpuBase);
                    res->Materials[entry->MaterialHandle] = *materialDesc;

                    gpu_material *gpuMaterials = (gpu_material*)res->MaterialBuffer.CpuBase;
                    gpu_material gpuMaterial = {};
                    gpuMaterials[entry->MaterialHandle] = gpuMaterial;
                }
            }
        }
    }
    EndSingleTimeCommands(context, cmd);

    DestroyBuffer(context, &staging);

    commands->CommandCounts[Load_Mesh] = 0;
    commands->CommandCounts[Load_Texture] = 0;
    commands->CommandCounts[Load_Cubemap] = 0;
    commands->CommandCounts[Load_Material] = 0;
}

internal void FillFrameGlobalsBuffer(vulkan_context *context, vulkan_resources *res, render_commands *commands)
{
    frame_globals *globals = (frame_globals *)res->Globals.Cpu;

    real32 FOVaspect = (real32)context->swapchainExtent.width / (real32)context->swapchainExtent.height;

    for (uint32 Type = Bind_Camera; Type <= Bind_Light; ++Type)
    {
        for (uint32 Index = 0; Index < commands->CommandCounts[Type]; ++Index)
        {
            command_type *cmdBase = commands->Commands[Type][Index];
            switch (*cmdBase)
            {
                case Bind_Light:
                {
                    command_bind_light *lightCmd = (command_bind_light *)cmdBase;

                    globals->LightDir = lightCmd->Direction;
                } break;

                case Bind_Camera:
                {
                    command_bind_camera *cameraCmd = (command_bind_camera *)cmdBase;

                    real32 nearPlane = 0.1f;
                    real32 farPlane  = 100.0f;

                    Matrix4 proj = Mat4Perspective(cameraCmd->FovY, FOVaspect, nearPlane, farPlane);

                    globals->ViewProj = Mat4Multiply(proj, cameraCmd->View);

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
}

internal void ExecuteRenderCommands(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, gpu_buffer *frameBuffer, render_pipeline *pipelines, render_commands *commands)
{
    PrepareMeshSortKeys(commands, res->Materials);

    render_pipeline *pipeline = 0;
    uint32 piplineId = Pipeline_Count;

    render_state current = {};
    render_state wanted  = {};

    vkCmdBindIndexBuffer(cmd, res->IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
    for (uint32 Index = 0; Index < commands->CommandCounts[Render_Skybox]; ++Index)
    {
        command_render_skybox *skyCmd = (command_render_skybox *)commands->Commands[Render_Skybox][Index];

        piplineId = Pipeline_Skybox;
        pipeline = &pipelines[piplineId];

        BindPipelineState(context, cmd, pipeline, &current, &wanted);

        skybox_params params = {};
        params.Tint         = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        params.CubemapIndex = skyCmd->CubemapHandle;

        PushPassParams(cmd, res, frameBuffer, params);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    render_queue Queues[] = { Queue_Opaque, Queue_Transparent, Queue_Overlay };
    for (uint32 QueueIndex = 0; QueueIndex < ArrayCount(Queues); ++QueueIndex)
    {
        for (uint32 Index = 0; Index < commands->CommandCounts[Render_Mesh]; ++Index)
        {
            command_render_mesh *meshCmd = commands->MeshCommands[Index].Command;

            gpu_mesh *mesh = &res->Meshes[meshCmd->MeshHandle];
            uint32 materialSlot = meshCmd->MaterialHandle;
            material *materialDesc = &res->Materials[materialSlot];

            if (materialDesc->Queue != Queues[QueueIndex])
            {
                continue;
            }

            if ((uint32)materialDesc->Pipeline != piplineId)
            {
                if (pipelines[materialDesc->Pipeline].Vert == VK_NULL_HANDLE)
                {
                    continue;
                }

                piplineId = (uint32)materialDesc->Pipeline;
                pipeline = &pipelines[piplineId];

                BindPipelineState(context, cmd, pipeline, &current, &wanted);
            }

            wanted.CullMode   = ToVulkanCullMode(materialDesc->PiplineState.CullMode);
            wanted.DepthTest  = materialDesc->PiplineState.DepthTest  ? VK_TRUE : VK_FALSE;
            wanted.DepthWrite = materialDesc->PiplineState.DepthWrite ? VK_TRUE : VK_FALSE;
            wanted.AlphaBlend = (materialDesc->PiplineState.BlendMode == Blend_Alpha) ? VK_TRUE : VK_FALSE;

            ApplyRenderState(context, cmd, &current, &wanted);

            VkDeviceAddress vertices    = res->VertexBuffer.Address;
            int32           firstVertex = (int32)mesh->FirstVertex;
            if (meshCmd->PosedVertices)
            {
                vertices    = BufferWrite(frameBuffer, meshCmd->PosedVertices, mesh->VertexCount * sizeof(vertex), 16).Gpu;
                firstVertex = 0;
            }

            draw_params params = {};
            params.Model         = meshCmd->Transform;
            params.Tint          = meshCmd->Tint;
            params.Vertices      = vertices;
            params.Materials     = res->MaterialBuffer.Address;
            params.MaterialSlot  = materialSlot;

            PushPassParams(cmd, res, frameBuffer, params);

            vkCmdDrawIndexed(cmd, mesh->IndexCount, 1, mesh->FirstIndex, firstVertex, 0);
        }
    }
}

internal void ExecuteUICommands(vulkan_context *context, VkCommandBuffer cmd, vulkan_resources *res, gpu_buffer *frameBuffer, render_pipeline *pipelines, render_commands *commands)
{
    render_pipeline *pipeline = &pipelines[Pipeline_UIRect];
    if (pipeline->Vert == VK_NULL_HANDLE)
    {
        return;
    }

    uint32 rectCount = commands->CommandCounts[Render_Rect];
    if (!rectCount)
    {
        return;
    }

    real32 width  = (real32)context->swapchainExtent.width;
    real32 height = (real32)context->swapchainExtent.height;

    gpu_buffer_slice slice = BufferAlloc(frameBuffer, rectCount * sizeof(rect_params), 16);

    rect_params *params = (rect_params *)slice.Cpu;

    for (uint32 Index = 0; Index < rectCount; ++Index)
    {
        command_render_rect *rectCmd = (command_render_rect *)commands->Commands[Render_Rect][Index];

        rect_params entry = {};
        entry.Rect = Vector4(rectCmd->Min.X / width  * 2.0f - 1.0f,
                             rectCmd->Min.Y / height * 2.0f - 1.0f,
                             rectCmd->Max.X / width  * 2.0f - 1.0f,
                             rectCmd->Max.Y / height * 2.0f - 1.0f);
        entry.UVRect = rectCmd->UV;
        entry.Tint = rectCmd->Color;
        entry.TextureSlot = rectCmd->TextureSlot;

        params[Index] = entry;
    }

    render_state current = {};
    render_state wanted  = {};
    BindPipelineState(context, cmd, pipeline, &current, &wanted);

    BindParams(cmd, res->PipelineLayout, slice.Gpu);

    vkCmdDraw(cmd, 6, rectCount, 0, 0);
}

internal void RenderVulkanFrame(vulkan_context *context, vulkan_resources *res, frame_targets *targets, vulkan_pipelines *pipelines, render_commands *Commands)
{
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
            ResizeRenderer(context, res, targets);
        }
        return;
    }

    gpu_buffer *frameBuffer = &res->FrameBuffers[Frame.Slot];
    VkImage swapchainImage = context->swapchainImages[Frame.ImageIndex];

    FillFrameGlobalsBuffer(context, res, Commands);
    BindDescriptorHeap(context, Frame.Cmd, res, res->PipelineLayout);

    BeginGpuFrame(context, &Frame);

    GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    {
    GpuSection(context, &Frame, "scene");

        BeginPass(context, Frame.Cmd, targets->Scene.View, targets->Depth.View, VK_ATTACHMENT_LOAD_OP_CLEAR, Vector4(0.05f, 0.05f, 0.08f, 1.0f), Depth_Clear);
        ExecuteRenderCommands(context, Frame.Cmd, res, frameBuffer, pipelines->Render, Commands);
        EndPass(Frame.Cmd);
    }

    {
    GpuSection(context, &Frame, "post");

        GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        BeginPass(context, Frame.Cmd, targets->Post.View, VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), Depth_None);
        DrawFullscreen(context, Frame.Cmd, res, frameBuffer, &pipelines->Render[Pipeline_Post], TEXTURE_SLOT_SCENE);
        EndPass(Frame.Cmd);
        GpuBarrier(Frame.Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        CmdAcquiredImageToGeneral(Frame.Cmd, swapchainImage);

    }

    {
    GpuSection(context, &Frame, "UI");

        BeginPass(context, Frame.Cmd, context->swapchainImageViews[Frame.ImageIndex], VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, Vector4(0.0f, 0.0f, 0.0f, 0.0f), Depth_None);
        DrawFullscreen(context, Frame.Cmd, res, frameBuffer, &pipelines->Render[Pipeline_UI], TEXTURE_SLOT_POST);
        ExecuteUICommands(context, Frame.Cmd, res, frameBuffer, pipelines->Render, Commands);
        EndPass(Frame.Cmd);
    }

    EndGpuFrame(&Frame);

    CmdImageToPresent(Frame.Cmd, swapchainImage);

    EndFrame(context, &Frame);

    if (Frame.NeedsResize)
    {
        ResizeRenderer(context, res, targets);
    }
}
