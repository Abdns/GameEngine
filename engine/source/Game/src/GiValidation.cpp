#pragma once

#include "GameState.h"
#include "RenderCommands.h"

enum gi_validation_scene
{
    GiValidation_Original = 0,
    GiValidation_ThinDivider,
    GiValidation_SideWindow,
    GiValidation_Materials,
    GiValidation_MovingBlocker,
    GiValidation_Count,
};

internal void InitGiValidation(game_state *GameState)
{
    materials *Materials = &GameState->Materials;
    Vector4 White = Vector4(0.75f, 0.75f, 0.75f, 1.0f);
    GameState->GiValidationMaterialHandles[0] = AddMaterial(Materials, LitMaterial(White, 0.0f, 0.8f));
    GameState->GiValidationMaterialHandles[1] = AddMaterial(Materials, LitMaterial(White, 0.0f, 0.22f));
    GameState->GiValidationMaterialHandles[2] = AddMaterial(Materials, LitMaterial(White, 1.0f, 0.22f));
    GameState->GiValidationScene = GiValidation_Original;
    GameState->GiValidationTime = 0.0f;
}

internal void ResetGiValidationCamera(game_state *GameState)
{
    Vector3 Position = Vector3(0.0f, 1.8f, 5.0f);
    if (GameState->GiValidationScene == GiValidation_SideWindow)
    {
        Position = Vector3(0.0f, 0.1f, 1.9f);
    }

    InitCamera(&GameState->Camera, MapIntoChunkSpace(GameState->World, WorldOrigin(), Position), DegToRad(75.0f));
    GameState->Camera.Pitch = GameState->GiValidationScene == GiValidation_SideWindow ? -0.05f : -0.24f;
    GameState->Camera.Yaw   = GameState->GiValidationScene == GiValidation_SideWindow ? -0.30f : 0.0f;
}

internal void SelectGiValidationScene(game_state *GameState, uint32 Scene)
{
    Assert(Scene < GiValidation_Count);
    if (Scene == GameState->GiValidationScene)
    {
        return;
    }

    if (GameState->GiValidationScene == GiValidation_Original)
    {
        GameState->GiSavedCamera = GameState->Camera;
    }

    GameState->GiValidationScene = Scene;
    GameState->GiValidationTime = 0.0f;
    if (Scene == GiValidation_Original)
    {
        GameState->Camera = GameState->GiSavedCamera;
    }
    else
    {
        ResetGiValidationCamera(GameState);
    }
}

internal void PushGiValidationMesh(game_state *GameState, render_commands *Commands, uint32 MeshIndex,
                                   Vector3 Position, Vector3 Size, uint32 MaterialIndex, Vector4 Tint)
{
    uint32 Mesh = GameState->SpawnMeshHandles[MeshIndex];
    asset_store *Assets = &GameState->Assets;
    Vector3 Min = Assets->MeshBoundsMin[Mesh];
    Vector3 Max = Assets->MeshBoundsMax[Mesh];
    Vector3 Center = 0.5f * (Min + Max);
    Vector3 Extent = Max - Min;
    Vector3 Scale = Vector3(Size.X / Maximum(Extent.X, 1e-5f),
                            Size.Y / Maximum(Extent.Y, 1e-5f),
                            Size.Z / Maximum(Extent.Z, 1e-5f));
    Matrix4 Transform = Mat4Multiply(Mat4Translation(Position.X, Position.Y, Position.Z),
                                     Mat4Multiply(Mat4Scale(Scale), Mat4Translation(-Center.X, -Center.Y, -Center.Z)));
    PushRenderMesh(Commands, Transform, Tint, Mesh, GameState->GiValidationMaterialHandles[MaterialIndex]);
}

internal void PushGiValidationScene(game_state *GameState, render_commands *Commands, real32 DeltaTime)
{
    if (!GameState->Paused)
    {
        GameState->GiValidationTime += DeltaTime;
    }

    camera *Camera = &GameState->Camera;
    Vector3 CameraPosition = WorldPositionToMeters(GameState->World, Camera->Position);
    PushRenderCamera(Commands, CameraView(Camera, CameraPosition), CameraPosition, CameraPosition, Camera->FovY);
    PushRenderLight(Commands, Vector3(0.4f, 1.0f, 0.3f), Vector3(3.0f, 2.85f, 2.6f));
    PushRenderSkybox(Commands, GameState->SkyHandle);

    Vector4 White = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    Vector4 Red   = Vector4(1.0f, 0.055f, 0.035f, 1.0f);
    Vector4 Green = Vector4(0.055f, 1.0f, 0.08f, 1.0f);
    Vector4 Gold  = Vector4(1.0f, 0.65f, 0.2f, 1.0f);

    PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, -1.6f, 0.0f), Vector3(5.0f, 0.2f, 5.0f), 0, White);

    switch (GameState->GiValidationScene)
    {
        case GiValidation_ThinDivider:
        {
            PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, 0.0f, -2.4f), Vector3(5.0f, 3.0f, 0.12f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(-1.8f, -0.4f, -0.2f), Vector3(0.12f, 2.2f, 3.0f), 0, Red);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(1.8f, -0.4f, -0.2f), Vector3(0.12f, 2.2f, 3.0f), 0, Green);
            // 3.5 cm is thinner than a GI voxel at the default volume settings.
            PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, -0.6f, -0.3f), Vector3(0.035f, 1.8f, 2.8f), 0, White);
            PushGiValidationMesh(GameState, Commands, 1, Vector3(-0.85f, -0.95f, -0.3f), Vector3(1.1f, 1.1f, 1.1f), 0, White);
            PushGiValidationMesh(GameState, Commands, 1, Vector3(0.85f, -0.95f, -0.3f), Vector3(1.1f, 1.1f, 1.1f), 0, White);
        } break;

        case GiValidation_SideWindow:
        {
            // Closed room with one real side opening; the camera starts inside.
            PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, 1.6f, 0.0f), Vector3(5.0f, 0.2f, 5.0f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, 0.0f, -2.4f), Vector3(4.8f, 3.0f, 0.2f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, 0.0f, 2.4f), Vector3(4.8f, 3.0f, 0.2f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(-2.4f, 0.0f, 0.0f), Vector3(0.2f, 3.0f, 4.8f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(2.4f, -1.1f, 0.0f), Vector3(0.2f, 0.8f, 4.8f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(2.4f, 1.1f, 0.0f), Vector3(0.2f, 0.8f, 4.8f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(2.4f, 0.0f, -1.65f), Vector3(0.2f, 1.4f, 1.5f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(2.4f, 0.0f, 1.65f), Vector3(0.2f, 1.4f, 1.5f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(-0.85f, -0.95f, -0.65f), Vector3(0.9f, 1.1f, 0.9f), 0, Red);
            PushGiValidationMesh(GameState, Commands, 1, Vector3(0.75f, -0.95f, -0.65f), Vector3(1.1f, 1.1f, 1.1f), 0, White);
        } break;

        case GiValidation_Materials:
        {
            PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, 0.0f, -2.4f), Vector3(5.0f, 3.0f, 0.12f), 0, White);
            // Same color and roughness: only metallic changes, left 0 / right 1.
            PushGiValidationMesh(GameState, Commands, 1, Vector3(-1.1f, -0.8f, 0.0f), Vector3(1.4f, 1.4f, 1.4f), 1, Gold);
            PushGiValidationMesh(GameState, Commands, 1, Vector3(1.1f, -0.8f, 0.0f), Vector3(1.4f, 1.4f, 1.4f), 2, Gold);
        } break;

        case GiValidation_MovingBlocker:
        {
            PushGiValidationMesh(GameState, Commands, 0, Vector3(0.0f, 0.0f, -2.4f), Vector3(5.0f, 3.0f, 0.12f), 0, White);
            real32 X = 1.3f * Sin(GameState->GiValidationTime * 1.2f);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(X, -0.4f, 0.3f), Vector3(0.6f, 2.2f, 0.45f), 0, White);
            PushGiValidationMesh(GameState, Commands, 0, Vector3(-1.6f, -0.95f, -0.8f), Vector3(0.9f, 1.1f, 0.9f), 0, Red);
            PushGiValidationMesh(GameState, Commands, 1, Vector3(1.3f, -0.95f, -0.6f), Vector3(1.1f, 1.1f, 1.1f), 0, White);
        } break;
    }
}
