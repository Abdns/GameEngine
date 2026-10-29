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

internal void InitMaterials(materials *Materials)
{
    *Materials = {};
}

internal uint32 AddMaterial(materials* Materials, material Material)
{
    if (Materials->Count >= MAX_GAME_MATERIALS)
    {
        Assert(!"Material array is full");
        return MATERIAL_NONE;
    }

    uint32 Index = Materials->Count++;
    Materials->Items[Index] = Material;

    return Index;
}

// The input array is borrowed. Entities and render commands copy it when stored.
internal material_params MatParams(uint32 Handle, const uint32 *TextureHandles, uint32 TextureCount, Vector4 Color)
{
    material_params Params = {};
    Params.SharedMatIndex = Handle;
    Params.TextureHandles = TextureHandles;
    Params.TextureCount = TextureCount;
    Params.Color = Color;

    return Params;
}

internal material UnlitMaterial()
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
    Material.PropPack = PiplineStateHash(State);

    return Material;
}

internal material OverlayMaterial()
{
    material Material = UnlitMaterial();
    Material.PiplineState.DepthTest  = false;
    Material.PiplineState.DepthWrite = false;
    Material.Queue = Queue_Overlay;
    Material.PropPack = PiplineStateHash(Material.PiplineState);

    return Material;
}

internal material LitMaterial()
{
    material Material = UnlitMaterial();
    Material.Pipeline = Pipeline_Lit;

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
