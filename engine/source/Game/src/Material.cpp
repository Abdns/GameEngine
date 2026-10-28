#pragma once

#include "Types.h"
#include "RenderCommands.h"
#include "RenderDescription.h"

#define MAX_GAME_MATERIALS 64

struct materials
{
    uint32   Count;
    material Items[MAX_GAME_MATERIALS];

    memory_arena MaterialArena;
};

internal materials InitMateriasls(materials Materials, memory_arena Arena)
{
    Materials.MaterialArena = Arena;

    return Materials;
}

internal uint32 AddMaterial(materials* Materials, material Material)
{
    Assert(Materials->Count < MAX_GAME_MATERIALS);

    uint32 Index = Materials->Count++;
    Materials->Items[Index] = Material;

    return Index;
}

internal material_params MatParams(uint32* TextureHandle, uint32 TextureCount, Vector4 Color)
{
    material_params Params = {};
    Params.TextureHandle = TextureHandle;
    Params.TextureCount = TextureCount;
    Params.Color = Color;

    return Params;
}

internal material UnlitMaterial(materials* materials, material_params params)
{
    pipline_state State = {};
    State.CullMode = Cull_None;
    State.BlendMode = Blend_Opaque;
    State.DepthTest = true;
    State.DepthWrite = true;

    material Material = {};
    Material.Pipeline      = Pipeline_Unlit;
    Material.PiplineState  = State;
    Material.Queue         = Queue_Opaque;
    Material.Params        = params;
    Material.PropPack = PiplineStateHash(State);

    Material.Handle = AddMaterial(materials, Material);

    return Material;
}

internal material OverlayMaterial(materials* materials, material_params params)
{
    material Material = UnlitMaterial(materials, params);
    Material.PiplineState.DepthTest  = false;
    Material.PiplineState.DepthWrite = false;
    Material.Queue = Queue_Overlay;

    return Material;
}

internal material LitMaterial(materials* materials, material_params params)
{
    material Material = UnlitMaterial(materials, params);
    Material.Pipeline = Pipeline_Lit;

    AddMaterial(materials, Material);

    return Material;
}


internal void PushMaterialsToRender(materials* Materials, render_commands* Commands)
{
    for (uint32 Index = 0; Index < Materials->Count; ++Index)
    {
        material* Material = &Materials->Items[Index];
        PushLoadMaterial(Commands, Index, Material);
    }
}
