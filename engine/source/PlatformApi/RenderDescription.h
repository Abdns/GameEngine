#ifndef RENDERDESCRIPTION_H
#define RENDERDESCRIPTION_H

#include "Types.h"
#include "EngineMath.h"

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

enum cull_mode
{
    Cull_None = 0,
    Cull_Back,
    Cull_Front,
};

enum texture_format
{
    TextureFormat_RGBA8 = 0,
    TextureFormat_RGBA16F,
};

#define TEXTURE_NONE 0xFFFFFFFF

enum blend_mode
{
    Blend_Opaque = 0,
    Blend_Alpha,
};

enum render_queue
{
    Queue_Opaque = 0,
    Queue_Transparent,
    Queue_Overlay,

    Queue_Count,
};

struct material
{
    pipeline_type Pipeline;

    cull_mode    CullMode;
    blend_mode   BlendMode;
    render_queue Queue;
    bool32       DepthTest;
    bool32       DepthWrite;

    Vector4 BaseColor;
    uint32  TextureHandle;
    uint32  Reserved[2]; // Preserve the serialized preset layout.
};

#endif // RENDERDESCRIPTION_H
