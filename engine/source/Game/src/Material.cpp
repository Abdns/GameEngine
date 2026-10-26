#pragma once

#include "Types.h"
#include "RenderCommands.h"
#include "RenderDescription.h"

#define MAX_GAME_MATERIALS 64

struct materials
{
    uint32   Count;
    material Items[MAX_GAME_MATERIALS];
};

internal material UnlitMaterial()
{
    pipline_state State = {};
    State.CullMode = Cull_None;
    State.BlendMode = Blend_Opaque;
    State.DepthTest = true;
    State.DepthWrite = true;



    material Result = {};
    Result.Pipeline      = Pipeline_Unlit;
    Result.PiplineState  = State;
    Result.Queue         = Queue_Opaque;
    Result.PropPack = PiplineStateHash(State);

    return Result;
}

internal material OverlayMaterial()
{
    material Result = UnlitMaterial();
    Result.PiplineState.DepthTest  = false;
    Result.PiplineState.DepthWrite = false;
    Result.Queue = Queue_Overlay;

    return Result;
}

internal material LitMaterial()
{
    material Result = UnlitMaterial();
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
        material* Material = &Materials->Items[Index];
        PushLoadMaterial(Commands, Index, Material);
    }
}
