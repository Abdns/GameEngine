#ifndef GAMESTATE_H
#define GAMESTATE_H

#include "Types.h"
#include "Memory.h"
#include "Input.h"
#include "AssetStore.cpp"
#include "Material.cpp"
#include "EntityStorage.cpp"
#include "Camera.cpp"
#include "UI.cpp"
#include "Gizmo.cpp"

struct game_state
{
    memory_arena WorldArena;
    memory_arena FrameArena;
    memory_arena MaterialsArena;

    world         *World;
    materials     *Materials;
    asset_store    Assets;
    entity_storage Storage;

    uint32 SkyHandle;

    camera Camera;

    input_state   Controls;
    ui_context    UI;
    gizmo_context Gizmo;

};

#endif
