#ifndef GAMESTATE_H
#define GAMESTATE_H

#include "Types.h"
#include "Memory.h"
#include "Input.h"
#include "AssetStore.cpp"
#include "Material.cpp"
#include "EntityStorage.cpp"
#include "Preset.cpp"
#include "Physics.cpp"
#include "Camera.cpp"
#include "UI.cpp"
#include "Gizmo.cpp"

struct game_state
{
    real32 tSine;

    memory_arena WorldArena;
    memory_arena FrameArena;

    world         *World;
    asset_store    Assets;
    materials      Materials;
    entity_storage Storage;
    preset_table   Presets;
    physics_state  Physics;

    uint32 SkyHandle;
    uint32 FontHandle;

    uint32 SpawnMeshHandles[2];
    uint32 SpawnMaterialHandles[3];

    camera Camera;

    input_state   Controls;
    ui_context    UI;
    gizmo_context Gizmo;

    bool32 Paused;
    bool32 ShowVoxels;
};

#endif
