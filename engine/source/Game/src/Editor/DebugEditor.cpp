#pragma once

#include "EditorUI.cpp"

internal void InitTools(game_state *GameState)
{
    asset_store *Assets    = &GameState->Assets;
    materials   *Materials = GameState->Materials;

    uint32 AxisMeshes[GIZMO_AXIS_COUNT];
    AxisMeshes[0] = GetAssetMeshHandle(Assets, "gizmo_axis_x");
    AxisMeshes[1] = GetAssetMeshHandle(Assets, "gizmo_axis_y");
    AxisMeshes[2] = GetAssetMeshHandle(Assets, "gizmo_axis_z");

    uint32 GizmoMaterial = OverlayMaterial(Materials);

    GameState->UI.Style = DefaultUIStyle();
    GameState->Gizmo.Style    = DefaultGizmoStyle(AxisMeshes, GizmoMaterial);
    GameState->Gizmo.Selected = ENTITY_STORAGE_NONE;

    uint32 FontTexture = GetAssetTextureHandle(&GameState->Assets, "font_atlas");
}

internal uint32 UpdateDebugEditor(game_state *GameState, sim_region *Region, render_commands *Commands)
{
    game_input *Controls = &GameState->Controls;
    gizmo_context *Gizmo = &GameState->Gizmo;
    if (!IsDebugMode(Controls))
    {
        Gizmo->Hot = 0;
        Gizmo->Active = 0;
        return ENTITY_STORAGE_NONE;
    }

    game_mouse_input *Mouse = &Controls->Mouse;
    BeginUI(&GameState->UI, Commands, Mouse, Controls->UIScale);
    DebugUI(GameState);

    Vector3 CameraSimP = WorldSubtract(GameState->World, &GameState->Camera.Position, &Region->Origin);
    ray PickRay = CameraRayFromScreen(&GameState->Camera, CameraSimP, Mouse->Position.X, Mouse->Position.Y, Controls->ViewportSize.X, Controls->ViewportSize.Y);
    UpdateGizmo(Gizmo, Mouse, &GameState->Assets, Region, PickRay, Commands);

    if (MouseAvailable(Mouse) && Mouse->Pressed)
    {
        Mouse->Consumed = true;
        GizmoSelect(Gizmo, PickEntity(PickRay, Region, &GameState->Assets));
    }
    return Gizmo->Selected;
}
