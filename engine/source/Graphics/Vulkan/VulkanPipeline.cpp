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

    Assert(shader.vert.Data && shader.frag.Data);

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
    Assert(result == VK_SUCCESS);

    pipeline.Vert = shaders[0];
    pipeline.Frag = shaders[1];

    FreeShader(&shader);

    return pipeline;
}

internal vulkan_pipelines CreatePipelines(vulkan_context *context, vulkan_resources *res)
{
    vulkan_pipelines pipelines = {};

    for (uint32 typeIndex = 0; typeIndex < Pipeline_Count; ++typeIndex)
    {
        pipelines.Render[typeIndex] = CreateRenderPipeline(context, res, pipline_shader((pipeline_type)typeIndex));
    }

    return pipelines;
}
