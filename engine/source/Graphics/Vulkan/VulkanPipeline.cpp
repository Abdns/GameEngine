#include "Strings.h"
#include "Vulkan.h"
#include "RenderDescription.h"

struct vulkan_shader
{
    file_data vert;
    file_data frag;
};

internal vulkan_shader LoadShader(const char *name)
{
    vulkan_shader shader = {};

    char vertPath[256];
    char fragPath[256];

    uint32 n = AppendString(vertPath, ArrayCount(vertPath), 0, "CompiledShaders/");
    n = AppendString(vertPath, ArrayCount(vertPath), n, name);
    AppendString(vertPath, ArrayCount(vertPath), n, ".vert.spv");

    n = AppendString(fragPath, ArrayCount(fragPath), 0, "CompiledShaders/");
    n = AppendString(fragPath, ArrayCount(fragPath), n, name);
    AppendString(fragPath, ArrayCount(fragPath), n, ".frag.spv");

    shader.vert = Win32ReadEntireFile(vertPath);
    shader.frag = Win32ReadEntireFile(fragPath);

    return shader;
}

internal void FreeShader(vulkan_shader *shader)
{
    Win32FreeFileMemory(shader->vert.Data);
    Win32FreeFileMemory(shader->frag.Data);
    *shader = {};
}

internal render_pipeline CreateRenderPipeline(vulkan_context *context, vulkan_resources *res, const char* shaderName)
{
    render_pipeline pipeline = {};
    pipeline.DefaultState.CullMode   = VK_CULL_MODE_NONE;
    pipeline.DefaultState.DepthTest  = VK_FALSE;
    pipeline.DefaultState.DepthWrite = VK_FALSE;
    pipeline.DefaultState.AlphaBlend = VK_FALSE;

    VkDescriptorSetLayout heapLayout = res->Heap.Layout;

    vulkan_shader shader = LoadShader(shaderName);
    if (!shader.vert.Data || !shader.frag.Data || shader.vert.Size < 20 || shader.frag.Size < 20 ||
        (shader.vert.Size & 3) || (shader.frag.Size & 3) ||
        *(uint32 *)shader.vert.Data != 0x07230203 || *(uint32 *)shader.frag.Data != 0x07230203)
    {
        DebugLog("[shaders] Invalid or missing SPIR-V for %s; keeping previous shaders.\n", shaderName);
        FreeShader(&shader);
        return {};
    }

    VkPushConstantRange pushRange = ParamsPushRange();

    VkShaderCreateInfoEXT createInfos[2] = {};

    createInfos[0].sType                  = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
    createInfos[0].flags                  = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
    createInfos[0].stage                  = VK_SHADER_STAGE_VERTEX_BIT;
    createInfos[0].nextStage              = VK_SHADER_STAGE_FRAGMENT_BIT;
    createInfos[0].codeType               = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    createInfos[0].codeSize               = shader.vert.Size;
    createInfos[0].pCode                  = shader.vert.Data;
    createInfos[0].pName                  = "VSMain";
    createInfos[0].setLayoutCount         = 1;
    createInfos[0].pSetLayouts            = &heapLayout;
    createInfos[0].pushConstantRangeCount = 1;
    createInfos[0].pPushConstantRanges    = &pushRange;

    createInfos[1].sType                  = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
    createInfos[1].flags                  = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
    createInfos[1].stage                  = VK_SHADER_STAGE_FRAGMENT_BIT;
    createInfos[1].codeType               = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    createInfos[1].codeSize               = shader.frag.Size;
    createInfos[1].pCode                  = shader.frag.Data;
    createInfos[1].pName                  = "PSMain";
    createInfos[1].setLayoutCount         = 1;
    createInfos[1].pSetLayouts            = &heapLayout;
    createInfos[1].pushConstantRangeCount = 1;
    createInfos[1].pPushConstantRanges    = &pushRange;

    VkShaderEXT shaders[2] = {};

    VkResult result = context->CreateShadersEXT(context->device, 2, createInfos, nullptr, shaders);
    if (result != VK_SUCCESS)
    {
        DebugLog("[shaders] Cannot create %s: VkResult=%d; keeping previous shaders.\n", shaderName, (int)result);
        for (uint32 Index = 0; Index < ArrayCount(shaders); ++Index)
        {
            if (shaders[Index]) context->DestroyShaderEXT(context->device, shaders[Index], nullptr);
        }
        FreeShader(&shader);
        return {};
    }

    pipeline.Vert = shaders[0];
    pipeline.Frag = shaders[1];

    FreeShader(&shader);

    return pipeline;
}

internal uint64 ShaderWriteTime()
{
    WIN32_FILE_ATTRIBUTE_DATA Data;
    if (!GetFileAttributesExA("CompiledShaders/shaders.ready", GetFileExInfoStandard, &Data)) return 0;
    return ((uint64)Data.ftLastWriteTime.dwHighDateTime << 32) | Data.ftLastWriteTime.dwLowDateTime;
}

internal void DestroyPipelines(vulkan_context *context, vulkan_pipelines *pipelines)
{
    for (uint32 typeIndex = 0; typeIndex < Pipeline_Count; ++typeIndex)
    {
        render_pipeline *pipeline = &pipelines->Render[typeIndex];
        if (pipeline->Vert) context->DestroyShaderEXT(context->device, pipeline->Vert, nullptr);
        if (pipeline->Frag) context->DestroyShaderEXT(context->device, pipeline->Frag, nullptr);
    }
    *pipelines = {};
}

internal bool32 TryCreatePipelines(vulkan_context *context, vulkan_resources *res, vulkan_pipelines *Result)
{
    vulkan_pipelines pipelines = {};
    for (uint32 typeIndex = 0; typeIndex < Pipeline_Count; ++typeIndex)
    {
        render_pipeline *pipeline = &pipelines.Render[typeIndex];
        *pipeline = CreateRenderPipeline(context, res, pipline_shader((pipeline_type)typeIndex));
        if (!pipeline->Vert || !pipeline->Frag)
        {
            DestroyPipelines(context, &pipelines);
            return false;
        }
    }
    pipelines.ShaderWriteTime = ShaderWriteTime();
    *Result = pipelines;
    return true;
}

internal vulkan_pipelines CreatePipelines(vulkan_context *context, vulkan_resources *res)
{
    vulkan_pipelines pipelines = {};
    bool32 Created = TryCreatePipelines(context, res, &pipelines);
    Assert(Created);
    return pipelines;
}

internal void ReloadShadersIfChanged(vulkan_context *context, vulkan_resources *res, vulkan_pipelines *pipelines)
{
#if ENGINE_INTERNAL
    uint64 Now = GetTickCount64();
    if (Now < pipelines->NextShaderCheck) return;
    pipelines->NextShaderCheck = Now + 250;

    // The build holds this file exclusively until every shader and the ready marker are published.
    HANDLE BuildGuard = CreateFileA("build.guard", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (BuildGuard == INVALID_HANDLE_VALUE) return;
    uint64 WriteTime = ShaderWriteTime();
    if (WriteTime && WriteTime != pipelines->ShaderWriteTime)
    {
        // Retry a rejected set only after the next successful shader build.
        pipelines->ShaderWriteTime = WriteTime;
        vulkan_pipelines Replacement = {};
        if (TryCreatePipelines(context, res, &Replacement))
        {
            VkResult Idle = vkDeviceWaitIdle(context->device);
            if (Idle == VK_SUCCESS)
            {
                DestroyPipelines(context, pipelines);
                *pipelines = Replacement;
                pipelines->NextShaderCheck = Now + 250;
                DebugLog("[shaders] Reloaded shader set.\n");
            }
            else
            {
                DestroyPipelines(context, &Replacement);
                DebugLog("[shaders] Cannot wait for GPU: VkResult=%d.\n", (int)Idle);
            }
        }
    }
    CloseHandle(BuildGuard);
#endif
}
