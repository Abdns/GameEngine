#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"

#define MAX_GAME_MATERIALS 64

struct materials
{
    uint32   Count;
    material Items[MAX_GAME_MATERIALS];
};

internal material UnlitMaterial(Vector4 BaseColor, uint32 TextureHandle)
{
    material Result = {};
    Result.Pipeline      = Pipeline_Unlit;
    Result.CullMode      = Cull_None;
    Result.BlendMode     = Blend_Opaque;
    Result.Queue         = Queue_Opaque;
    Result.DepthTest     = true;
    Result.DepthWrite    = true;
    Result.BaseColor     = BaseColor;
    Result.TextureHandle = TextureHandle;

    return Result;
}

internal material OverlayMaterial(Vector4 BaseColor, uint32 TextureHandle)
{
    material Result = UnlitMaterial(BaseColor, TextureHandle);
    Result.DepthTest  = false;
    Result.DepthWrite = false;
    Result.Queue      = Queue_Overlay;

    return Result;
}

internal material LitMaterial(Vector4 BaseColor, uint32 TextureHandle)
{
    material Result = UnlitMaterial(BaseColor, TextureHandle);
    Result.Pipeline = Pipeline_Lit;

    return Result;
}

internal uint32 AddMaterial(materials* Materials, material Material)
{
    Assert(Materials->Count < MAX_GAME_MATERIALS);
    uint32 Index = Materials->Count++;

    Materials->Items[Index] = Material;

    return Index;
}

internal void PushMaterialsToRender(materials* Materials, render_commands* Commands)
{
    for (uint32 Index = 0; Index < Materials->Count; ++Index)
    {
        material* Material = Materials->Items + Index;
        PushLoadMaterial(Commands, Index, Material);
    }
}
