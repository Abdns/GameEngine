#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"
#include "GameState.h"
#include "EntitySpawn.cpp"

#include <stdio.h>

internal const char *EntityTypeName(entity_type Type)
{
    switch (Type)
    {
        case Entity_Floor: return "floor";
        case Entity_Prop:  return "prop";
        case Entity_Ball:  return "ball";
        default:           return "null";
    }
}

internal void UILabelVector3(ui_layout *Layout, const char *Label, Vector3 Value)
{
    char Buffer[64];

    snprintf(Buffer, sizeof(Buffer), "%s %.1f %.1f %.1f", Label, Value.X, Value.Y, Value.Z);

    UILabel(Layout, Buffer);
}

internal void UpdateEntityInfoPanel(game_state *GameState, ui_context *UI)
{
    low_entity *Entity = GetLowEntity(&GameState->Storage, GameState->Gizmo.Selected);

    if (!Entity)
    {
        return;
    }

    ui_layout Layout = UIBeginPanelAnchored(UI, UIAnchor_BottomLeft, Vector2(20.0f, 20.0f), 180.0f);

    UILabel(&Layout, Entity->Name);
    UILabel(&Layout, EntityTypeName(Entity->SimVariant.Type));

    UILabelVector3(&Layout, "pos", WorldPositionToMeters(GameState->World, Entity->Position));

    UIEndPanel(&Layout);
}

internal void UpdateDebugPanel(game_state *GameState, ui_context *UI)
{
    ui_layout Layout = UIBeginPanel(UI, Vector2(20.0f, 20.0f), 140.0f);

    UICheckBox(&Layout, "pause", &GameState->Paused);

    UIEndPanel(&Layout);
}

internal void UpdateVoxelPanel(game_state *GameState, ui_context *UI)
{
    ui_layout Layout = UIBeginPanelAnchored(UI, UIAnchor_BottomRight, Vector2(20.0f, 20.0f), 140.0f);

    if (UIButton(&Layout, "voxels"))
    {
        GameState->ShowVoxels = !GameState->ShowVoxels;
    }

    UIEndPanel(&Layout);

    if (GameState->ShowVoxels)
    {
        PushVolumeDebug(UI->Commands);
    }
}

internal void EntityInfoPanel(game_state* GameState, ui_context* UI)
{
    ui_layout Layout = UIBeginPanelAnchored(UI, UIAnchor_TopRight, Vector2(20.0f, 20.0f), 140.0f);

    UIEndPanel(&Layout);
}

internal void UpdateSpawnPanel(game_state *GameState, ui_context *UI)
{
    ui_layout Layout = UIBeginPanelAnchored(UI, UIAnchor_TopRight, Vector2(20.0f, 20.0f), 140.0f);

    if (UIButton(&Layout, "spawn"))
    {
        AddEntityFromPreset(GameState, GetPresetIndex(&GameState->Presets, "cube"), WorldOrigin(), Vector3(0.0f, 0.0f, 0.0f));
    }

    if (UIButton(&Layout, "clear"))
    {
        ClearSpawnedEntities(GameState);
    }

    UIEndPanel(&Layout);
}

internal void UpdateEditorUI(game_state *GameState, ui_context *UI)
{
    UpdateDebugPanel(GameState, UI);
    UpdateVoxelPanel(GameState, UI);
    UpdateSpawnPanel(GameState, UI);
    UpdateEntityInfoPanel(GameState, UI);
}
