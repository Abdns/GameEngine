#include "Strings.h"
#include "Vulkan.h"

struct vulkan_shader
{
    file_data vert;
    file_data frag;
};

struct render_state
{
    VkCullModeFlags CullMode;
    VkBool32        DepthTest;
    VkBool32        DepthWrite;
    VkBool32        AlphaBlend;
    bool32          Valid;
};

struct pipeline_desc
{
    const char *ShaderName;

    VkBool32 DepthTest;
    VkBool32 DepthWrite;
    VkBool32 Blend;
};

struct render_pipeline
{
    render_state DefaultState;

    VkShaderEXT Vert;
    VkShaderEXT Frag;
};

struct compute_pipeline
{
    VkShaderEXT Compute;
};

enum compute_type
{
    Compute_VoxelizeClear = 0,
    Compute_VoxelizeMesh,
    Compute_VoxelizeResolve,

    Compute_SkyOcclusionSweep,
    Compute_SkyOcclusionBlur,

    Compute_RadianceClear,
    Compute_RadianceInject,
    Compute_RadianceSmooth,

    Compute_CascadesTrace,
    Compute_CascadesMerge,
    Compute_CascadesResolve,
    Compute_CascadesPrefilter,

    Compute_ScreenGiProbe,

    Compute_Count,
};

struct compute_desc
{
    const char *File;
    const char *Entry;
};

global_variable render_pipeline  Pipelines[Pipeline_Count];
global_variable compute_pipeline ComputePipelines[Compute_Count];

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

global_variable compute_desc ComputeDescs[] =
{
    { "voxelize",     "Clear"     },
    { "voxelize",     "Mesh"      },
    { "voxelize",     "Resolve"   },

    { "skyocclusion", "Sweep"     },
    { "skyocclusion", "Blur"      },

    { "radiance",     "Clear"     },
    { "radiance",     "Inject"    },
    { "radiance",     "Smooth"    },

    { "cascades",     "Trace"     },
    { "cascades",     "Merge"     },
    { "cascades",     "Resolve"   },
    { "cascades",     "Prefilter" },

    { "screengi",     "Probe"     },
};

static_assert(ArrayCount(PipelineDescs) == Pipeline_Count, "PipelineDescs must describe every pipeline_type");
static_assert(ArrayCount(ComputeDescs) == Compute_Count, "ComputeDescs must describe every compute_type");

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

    DebugLog("Shader '%s' loaded (vert %u, frag %u bytes)\n",name, shader.vert.Size, shader.frag.Size);

    return shader;
}

internal void FreeShader(vulkan_shader *shader)
{
    Win32FreeFileMemory(shader->vert.Data);
    Win32FreeFileMemory(shader->frag.Data);
    *shader = {};
}

internal file_data LoadComputeShader(compute_desc *desc)
{
    char path[256];

    uint32 n = AppendString(path, ArrayCount(path), 0, "CompiledShaders/");
    n = AppendString(path, ArrayCount(path), n, desc->File);
    n = AppendString(path, ArrayCount(path), n, "_");
    n = AppendString(path, ArrayCount(path), n, desc->Entry);
    AppendString(path, ArrayCount(path), n, ".comp.spv");

    file_data code = Win32ReadEntireFile(path);

    Assert(code.Data);

    DebugLog("Compute shader '%s.hlsl:%s' loaded (%u bytes)\n", desc->File, desc->Entry, code.Size);

    return code;
}

internal void CreateComputePipeline(vulkan_context *context, vulkan_resources *res, compute_pipeline *pipeline, compute_desc *desc)
{
    VkDescriptorSetLayout heapLayout = res->Heap.Layout;

    file_data code = LoadComputeShader(desc);

    VkPushConstantRange pushRange = ParamsPushRange();

    VkShaderCreateInfoEXT createInfo{};
    createInfo.sType                  = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
    createInfo.stage                  = VK_SHADER_STAGE_COMPUTE_BIT;
    createInfo.codeType               = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    createInfo.codeSize               = code.Size;
    createInfo.pCode                  = code.Data;
    createInfo.pName                  = desc->Entry;
    createInfo.setLayoutCount         = 1;
    createInfo.pSetLayouts            = &heapLayout;
    createInfo.pushConstantRangeCount = 1;
    createInfo.pPushConstantRanges    = &pushRange;

    VkShaderEXT shader = VK_NULL_HANDLE;

    VkResult result = context->CreateShadersEXT(context->device, 1, &createInfo, nullptr, &shader);
    Assert(result == VK_SUCCESS);

    pipeline->Compute = shader;

    Win32FreeFileMemory(code.Data);
}

internal void CreateRenderPipeline(vulkan_context *context, vulkan_resources *res, render_pipeline *pipeline, pipeline_desc *desc)
{
    pipeline->DefaultState.CullMode   = VK_CULL_MODE_NONE;
    pipeline->DefaultState.DepthTest  = desc->DepthTest;
    pipeline->DefaultState.DepthWrite = desc->DepthWrite;
    pipeline->DefaultState.AlphaBlend = desc->Blend;

    VkDescriptorSetLayout heapLayout = res->Heap.Layout;

    vulkan_shader shader = LoadShader(desc->ShaderName);

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

    pipeline->Vert = shaders[0];
    pipeline->Frag = shaders[1];

    FreeShader(&shader);
}

internal void CreatePipelines(vulkan_context *context, vulkan_resources *res)
{
    for (uint32 i = 0; i < Pipeline_Count; ++i)
    {
        CreateRenderPipeline(context, res, &Pipelines[i], &PipelineDescs[i]);
    }

    for (uint32 i = 0; i < Compute_Count; ++i)
    {
        CreateComputePipeline(context, res, &ComputePipelines[i], &ComputeDescs[i]);
    }
}
