#ifndef RENDERDESCRIPTION_H
#define RENDERDESCRIPTION_H

#include "Types.h"
#include "EngineMath.h"
#include "ImageFormat.h"

#define TEXTURE_NONE 0xFFFFFFFF

enum pipeline_type
{
    Pipeline_Unlit = 0,
    Pipeline_Lit,
    Pipeline_Skybox,
    Pipeline_Post,
    Pipeline_UI,
    Pipeline_UIRect,
    Pipeline_Count,

    Pipeline_MeshCount = Pipeline_Skybox,
};

constexpr const char* pipline_shader(pipeline_type type)
{
    switch (type)
    {
        case Pipeline_Unlit:  return "unlit";
        case Pipeline_Lit:    return "lit";
        case Pipeline_Skybox: return "skybox";
        case Pipeline_Post:   return "post";
        case Pipeline_UI:     return "UI";
        case Pipeline_UIRect: return "uirect";
        default:              return "";
    }
}


enum cull_mode
{
    Cull_None = 0,
    Cull_Back,
    Cull_Front,
};

enum blend_mode
{
    Blend_Opaque = 0,
    Blend_Alpha,
};

enum render_queue
{
    Queue_Opaque = 0,
    Queue_Transparent = 100,
    Queue_Overlay = 200,

    Queue_Count,
};

struct pipline_state
{
    cull_mode    CullMode;
    blend_mode   BlendMode;
    bool32       DepthTest;
    bool32       DepthWrite;
};

uint16 PiplineStateHash(pipline_state State)
{
    uint16 Hash = 0;

    Hash |= ((uint16)State.CullMode & 0xF) << 0;
    Hash |= ((uint16)State.BlendMode & 0xF) << 4;
    Hash |= ((uint16)State.DepthTest & 0xF) << 8;
    Hash |= ((uint16)State.DepthWrite & 0xF) << 12;

    return Hash;
}

struct material
{
    pipeline_type Pipeline;
    pipline_state PiplineState;
    render_queue Queue;
    uint32 PropPack;
};

#endif
