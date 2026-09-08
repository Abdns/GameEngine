#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"
#include "GameState.h"
#include "EntitySpawn.cpp"
#include "GiValidation.cpp"

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
    {
        ui_layout listLayout = UIScrollList(&Layout, 3);
        {
            UILabel(&listLayout, Entity->Name);
            UILabel(&listLayout, EntityTypeName(Entity->SimVariant.Type));
            UIButton(&listLayout, "ss");
            UIButton(&listLayout, "ss");
            UIButton(&listLayout, "ss");

            UILabelVector3(&listLayout, "pos", WorldPositionToMeters(GameState->World, Entity->Position));
        }
        UIEndScrollList(&listLayout);

        UIButton(&Layout, "ss");
        UIButton(&Layout, "ss");
        UIButton(&Layout, "ss");
        UIButton(&Layout, "ss");
    }
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
    ui_layout Layout = UIBeginPanelAnchored(UI, UIAnchor_BottomRight, Vector2(20.0f, 20.0f), 350.0f);

    static const char *DebugModes[] =
    {
        "Final", "Direct", "Diffuse GI", "Sky visibility",
        "Screen confidence", "History rejected", "Sky specular",
    };
    static const char *Scenes[] =
    {
        "Original", "Thin divider", "Side window", "Materials", "Moving blocker",
    };
    char Label[64];

    if (UIButton(&Layout, GameState->ShowGiDiagnostics ? "Close GI diagnostics" : "GI diagnostics"))
    {
        GameState->ShowGiDiagnostics = !GameState->ShowGiDiagnostics;
    }
    if (!GameState->ShowGiDiagnostics)
    {
        UIEndPanel(&Layout);
        if (GameState->ShowVoxels)
        {
            PushVolumeDebug(UI->Commands);
        }
        return;
    }
    snprintf(Label, sizeof(Label), "View: %s", DebugModes[GameState->GiDebugMode % ArrayCount(DebugModes)]);
    if (UIButton(&Layout, Label))
    {
        GameState->GiDebugMode = (GameState->GiDebugMode + 1) % ArrayCount(DebugModes);
    }

    snprintf(Label, sizeof(Label), "History %.3f s", GameState->GiHistorySeconds);
    UIDragReal32(&Layout, Label, &GameState->GiHistorySeconds, 0.001f);
    GameState->GiHistorySeconds = Clamp(0.0f, GameState->GiHistorySeconds, 0.5f);

    snprintf(Label, sizeof(Label), "GI strength %.2f", GameState->GiStrength);
    UIDragReal32(&Layout, Label, &GameState->GiStrength, 0.01f);
    GameState->GiStrength = Clamp(0.0f, GameState->GiStrength, 2.0f);
    UILabel(&Layout, "Drag values to adjust");

    if (UIButton(&Layout, "Reset GI controls"))
    {
        GameState->GiDebugMode = 0;
        GameState->GiHistorySeconds = 0.08f;
        GameState->GiStrength = 1.0f;
    }

    snprintf(Label, sizeof(Label), "Scene: %s", Scenes[GameState->GiValidationScene]);
    if (UIButton(&Layout, Label))
    {
        SelectGiValidationScene(GameState, (GameState->GiValidationScene + 1) % GiValidation_Count);
    }

    if (GameState->GiValidationScene != GiValidation_Original)
    {
        if (GameState->GiValidationScene == GiValidation_Materials)
        {
            UILabel(&Layout, "Left: dielectric");
            UILabel(&Layout, "Right: metal");
        }
        if (UIButton(&Layout, "Reset lab camera"))
        {
            ResetGiValidationCamera(GameState);
        }
        if (UIButton(&Layout, "Back to original scene"))
        {
            SelectGiValidationScene(GameState, GiValidation_Original);
        }
    }

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
    if (GameState->GiValidationScene == GiValidation_Original)
    {
        UpdateSpawnPanel(GameState, UI);
        UpdateEntityInfoPanel(GameState, UI);
    }
}
