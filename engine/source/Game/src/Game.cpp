#include "Game.h"

#include "AssetStore.cpp"
#include "SimEntity.cpp"
#include "World.cpp"
#include "EntityStorage.cpp"
#include "SimRegion.cpp"
#include "Shapes.cpp"
#include "Broadphase.cpp"
#include "Physics.cpp"
#include "Raycast.cpp"
#include "Input.cpp"
#include "Camera.cpp"
#include "Material.cpp"
#include "Text.cpp"
#include "UI.cpp"
#include "Gizmo.cpp"

#include "GameState.h"

#include "EntitySpawn.cpp"

#define FRAME_ARENA_SIZE      Megabytes(8)
#define WORLD_CHUNK_DIM       16.0f
#define ENTITY_CAPACITY       4096
#define SIM_MAX_ENTITIES      4096
#define SIM_HALF_DIM          48.0f

internal void GameOutputSound(game_state* GameState, game_sound_output_buffer* SoundBuffer)
{
}

internal texture_format TextureFormatFromAsset(uint32 AssetFormat)
{
    return (AssetFormat == (uint32)ImageFormat_RGBA16F) ? TextureFormat_RGBA16F : TextureFormat_RGBA8;
}

internal void PushAssetsToRender(asset_store *Assets, render_commands *Commands)
{
    for (uint32 Handle = 0; Handle < Assets->MeshCount; ++Handle)
    {
        PushLoadMesh(Commands, Handle, AssetMeshVertices(Assets, Handle), Assets->MeshVertexCount[Handle], AssetMeshIndices(Assets, Handle), Assets->MeshIndexCount[Handle]);
    }

    for (uint32 Handle = 0; Handle < Assets->TextureCount; ++Handle)
    {
        PushLoadTexture(Commands, Handle, AssetTexturePixels(Assets, Handle), Assets->TextureWidth[Handle], Assets->TextureHeight[Handle], Assets->TextureSRGB[Handle], TextureFormatFromAsset(Assets->TextureFormat[Handle]));
    }

    for (uint32 Handle = 0; Handle < Assets->CubemapCount; ++Handle)
    {
        PushLoadCubemap(Commands, Handle, AssetCubemapPixels(Assets, Handle), Assets->CubemapFaceSize[Handle], TextureFormatFromAsset(Assets->CubemapFormat[Handle]));
    }
}

internal void LoadAssetPack(const char *Name, game_memory *Memory, game_state *GameState)
{
    file_data PackFile = Memory->PlatformReadEntireFile((char *)Name);

    asset_store_budget Budget = AssetStoreBudgetFromPack(PackFile.Data, PackFile.Size);

    AssetStoreInit(&GameState->Assets, &GameState->WorldArena, Budget);
    AssetStoreLoadPack(&GameState->Assets, PackFile.Data, PackFile.Size);

    Memory->PlatformFreeFileMemory(PackFile.Data);
}

internal void BuildMeshShapes(game_state *GameState, memory_arena *Arena)
{
    asset_store *Assets = &GameState->Assets;

    for (uint32 MeshHandle = 0; MeshHandle < Assets->MeshCount; ++MeshHandle)
    {
        PhysicsSetMeshShape(&GameState->Physics, Arena, MeshHandle, AssetCollisionMesh(Assets, MeshHandle));
    }
}

internal void PushEntitiesToRender(sim_region *Region, render_commands *Commands, real32 Alpha, uint32 HighlightIndex, Vector4 HighlightColor)
{
    for (uint32 Index = 0; Index < Region->EntityCount; ++Index)
    {
        sim_entity *Entity = Region->Entities + Index;

        if (!(Entity->Flags & EntityFlag_Visible))
        {
            continue;
        }

        Vector4 Tint = Entity->Tint;
        if (Entity->LowStorageIndex == HighlightIndex)
        {
            Tint = Vector4(HighlightColor.X, HighlightColor.Y, HighlightColor.Z, Tint.W);
        }

        PushRenderMesh(Commands, SimEntityRenderTransform(Entity, Alpha), Tint, Entity->MeshHandle, Entity->MaterialHandle);
    }
}

internal void UpdateDebugPanel(game_state *GameState, ui_context *UI)
{
    asset_store *Assets = &GameState->Assets;
    uint32       Font   = GameState->FontHandle;

    UILabledCheckBox(UI, Assets, Font, "pause", RectMinDim(20.0f, 20.0f, 140.0f, 36.0f), &GameState->Paused);

    if (UILabeledButton(UI, Assets, Font, "spawn", RectMinDim(20.0f, 66.0f, 140.0f, 36.0f)))
    {
        AddEntity(GameState, Entity_Prop, TransformAt(Vector3(0.0f, 3.0f, 0.0f)), GameState->SpawnMeshHandles[0], GameState->SpawnMaterialHandles[0], false, 0);
    }

    if (UILabeledButton(UI, Assets, Font, "clear", RectMinDim(20.0f, 112.0f, 140.0f, 36.0f)))
    {
        ClearSpawnedEntities(GameState);
    }
}

internal void StepPhysics(game_state *GameState, sim_region *Region, real32 dtForFrame)
{
    PhysicsAccumulate(&GameState->Physics, dtForFrame);

    while (PhysicsNextTick(&GameState->Physics))
    {
        SimSavePreviousTransforms(Region);

        if (!GameState->Paused)
        {
            PhysicsStep(&GameState->Physics, Region);
        }
    }
}

internal uint32 PickAndShoot(game_state *GameState, sim_region *Region, ray PickRay, real32 Alpha)
{
    mouse_input *Mouse = &GameState->Controls.Mouse;

    uint32 HitIndex = ENTITY_STORAGE_NONE;

    if (!MouseAvailable(Mouse) || !(Mouse->Pressed || Mouse->MiddlePressed))
    {
        return HitIndex;
    }

    Mouse->Consumed = true;

    if (Mouse->Pressed)
    {
        raycast_hit Hit;
        HitIndex = RayCastSim(PickRay, Region, &GameState->Assets, &GameState->FrameArena, Alpha, &Hit) ? Hit.StorageIndex : ENTITY_STORAGE_NONE;

        GizmoSelect(&GameState->Gizmo, HitIndex);
    }

    if (Mouse->MiddlePressed)
    {
        ShootBall(GameState, Region, PickRay);
    }

    return HitIndex;
}

internal void InitTools(game_state *GameState)
{
    asset_store *Assets    = &GameState->Assets;
    materials   *Materials = &GameState->Materials;

    uint32 AxisMeshes[GIZMO_AXIS_COUNT];
    AxisMeshes[0] = GetAssetMeshHandle(Assets, "gizmo_axis_x");
    AxisMeshes[1] = GetAssetMeshHandle(Assets, "gizmo_axis_y");
    AxisMeshes[2] = GetAssetMeshHandle(Assets, "gizmo_axis_z");

    uint32 GizmoMaterial = AddMaterial(Materials, OverlayMaterial(Vector4(1.0f, 1.0f, 1.0f, 1.0f), GetAssetTextureHandle(Assets, "test")));

    GameState->UI.Style       = DefaultUIStyle();
    GameState->Gizmo.Style    = DefaultGizmoStyle(AxisMeshes, GizmoMaterial);
    GameState->Gizmo.Selected = ENTITY_STORAGE_NONE;
}

internal void BuildTestScene(game_state *GameState)
{
    asset_store *Assets    = &GameState->Assets;
    materials   *Materials = &GameState->Materials;

    uint32 SphereMesh = GameState->SpawnMeshHandles[1];

    GameState->SpawnMaterialHandles[0] = AddMaterial(Materials, UnlitMaterial(Vector4(1.0f, 1.0f, 1.0f, 1.0f), GetAssetTextureHandle(Assets, "test")));
    GameState->SpawnMaterialHandles[1] = GameState->SpawnMaterialHandles[0];
    GameState->SpawnMaterialHandles[2] = GameState->SpawnMaterialHandles[0];

    uint32 LitHandle   = AddMaterial(Materials, LitMaterial(Vector4(0.9f, 0.5f, 0.2f, 1.0f), 1.0f, 0.25f));
    uint32 FloorHandle = AddMaterial(Materials, LitMaterial(Vector4(0.45f, 0.45f, 0.5f, 1.0f), 0.0f, 0.7f));

    AddEntity(GameState, Entity_Floor, TransformAt(Vector3(0.0f, -2.1f, 0.0f)), GetAssetMeshHandle(Assets, "plane"), FloorHandle, true, "floor");

    for (uint32 Row = 0; Row < 2; ++Row)
    {
        real32 Metallic = (real32)Row;

        for (uint32 Column = 0; Column < 7; ++Column)
        {
            real32 Roughness = 0.05f + (real32)Column * 0.15f;

            uint32 BallMaterial = AddMaterial(Materials, LitMaterial(Vector4(0.75f, 0.05f, 0.05f, 1.0f), Metallic, Roughness));

            Vector3 Position = Vector3(((real32)Column - 3.0f) * 1.3f, Metallic * 1.6f - 0.8f, -4.0f);

            AddEntity(GameState, Entity_Prop, TransformAt(Position), SphereMesh, BallMaterial, true, 0);
        }
    }

    for (uint32 SpawnIndex = 0; SpawnIndex < 3; ++SpawnIndex)
    {
        real32 Angle  = (real32)SpawnIndex * 2.39996f;
        real32 Radius = 0.9f * SquareRoot((real32)SpawnIndex + 1.0f);

        Vector3 Position = Vector3(Cos(Angle) * Radius, 0.0f, Sin(Angle) * Radius);

        uint32 Mesh     = GameState->SpawnMeshHandles[SpawnIndex % ArrayCount(GameState->SpawnMeshHandles)];
        uint32 Material = GameState->SpawnMaterialHandles[SpawnIndex % ArrayCount(GameState->SpawnMaterialHandles)];

        AddEntity(GameState, Entity_Prop, TransformAt(Position), Mesh, Material, false, 0);
    }

    AddEntity(GameState, Entity_Prop, TransformAt(Vector3(-2.5f, 0.0f, 0.0f)), GameState->SpawnMeshHandles[1], LitHandle, false, 0);
    AddEntity(GameState, Entity_Prop, TransformAt(Vector3( 2.5f, 0.0f, 0.0f)), GameState->SpawnMeshHandles[0], LitHandle, false, 0);
}

internal void InitGame(game_memory *Memory, game_state *GameState, render_commands *RenderCommands)
{
    InitializeArena(&GameState->WorldArena, Memory->PermanentStorageSize - sizeof(game_state), (uint8 *)Memory->PermanentStorage + sizeof(game_state));

    memory_arena *WorldArena = &GameState->WorldArena;
    SubArena(&GameState->FrameArena, WorldArena, FRAME_ARENA_SIZE);

    LoadAssetPack(ENGA_PACK_PATH, Memory, GameState);

    asset_store *Assets = &GameState->Assets;

    PushAssetsToRender(Assets, RenderCommands);

    GameState->World = PushStruct(WorldArena, world);
    WorldInit(GameState->World, WORLD_CHUNK_DIM);

    EntityStorageInit(&GameState->Storage, WorldArena, ENTITY_CAPACITY);

    PhysicsInit(&GameState->Physics, WorldArena, SIM_MAX_ENTITIES, Assets->MeshCount);
    BuildMeshShapes(GameState, WorldArena);

    GameState->SkyHandle  = GetAssetCubemapHandle(Assets, "sky");
    GameState->FontHandle = GetAssetFontHandle(Assets, "DejaVuSansMono24");

    GameState->SpawnMeshHandles[0] = GetAssetMeshHandle(Assets, "cube");
    GameState->SpawnMeshHandles[1] = GetAssetMeshHandle(Assets, "sphere");

    InitTools(GameState);
    BuildTestScene(GameState);

    PushMaterialsToRender(&GameState->Materials, RenderCommands);

    InitCamera(&GameState->Camera, MapIntoChunkSpace(GameState->World, WorldOrigin(), Vector3(0.0f, 0.0f, 4.0f)), DegToRad(75.0f));

    GameState->tSine  = 0.0f;
    GameState->Paused = false;

    DebugLog("World arena: %llu KB used of %llu KB\n", WorldArena->Used / 1024, WorldArena->Size / 1024);
}

extern "C" __declspec(dllexport)
GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Assert(sizeof(game_state) <= Memory->PermanentStorageSize);

    game_state *GameState = (game_state *)Memory->PermanentStorage;

    if (!Memory->IsInitialized)
    {
        InitGame(Memory, GameState, RenderCommands);

        Memory->IsInitialized = true;
    }

    ResetArena(&GameState->FrameArena);

    camera        *Camera   = &GameState->Camera;
    ui_context    *UI       = &GameState->UI;
    gizmo_context *Gizmo    = &GameState->Gizmo;
    input_state   *Controls = &GameState->Controls;
    mouse_input   *Mouse    = &Controls->Mouse;

    BeginInput(Controls, Input);
    BeginUI(UI, Mouse, RenderCommands);
    BeginGizmo(Gizmo, Mouse, RenderCommands);

    UpdateDebugPanel(GameState, UI);

    rectangle3  SimBounds = Rect3CenterRadius(Vector3(0.0f, 0.0f, 0.0f), SIM_HALF_DIM);
    sim_region *Region    = BeginSim(&GameState->FrameArena, GameState->World, &GameState->Storage, Camera->Position, SimBounds, SIM_MAX_ENTITIES);
    {
        StepPhysics(GameState, Region, Controls->dtForFrame);

        real32 RenderAlpha = PhysicsRenderAlpha(&GameState->Physics);

        UpdateCamera(Camera, GameState->World, Controls);

        Vector3 CameraSimP = WorldSubtract(GameState->World, &Camera->Position, &Region->Origin);

        ray PickRay = CameraRayFromScreen(Camera, CameraSimP, Mouse->Position.X, Mouse->Position.Y, Controls->ViewportSize.X, Controls->ViewportSize.Y);

        sim_entity *Selected = GetEntityByStorageIndex(Region, Gizmo->Selected);
        if (Selected)
        {
            Vector3 GizmoPosition = SimEntityRenderPosition(Selected, RenderAlpha);

            if (Axis(Gizmo, &GameState->Assets, PickRay, &GizmoPosition))
            {
                SimEntitySetPosition(Selected, GizmoPosition);
            }
        }

        PickAndShoot(GameState, Region, PickRay, RenderAlpha);

        Vector3 CameraWorldP = WorldPositionToMeters(GameState->World, Region->Origin) + CameraSimP;

        PushRenderCamera(RenderCommands, CameraView(Camera, CameraSimP), CameraSimP, CameraWorldP, Camera->FovY);
        PushRenderLight(RenderCommands, Vector3(0.4f, 1.0f, 0.3f), Vector3(3.0f, 2.85f, 2.6f));
        PushRenderSkybox(RenderCommands, GameState->SkyHandle);
        PushEntitiesToRender(Region, RenderCommands, RenderAlpha, Gizmo->Selected, Gizmo->Style.Selected);
    }
    EndSim(Region, &GameState->WorldArena);

    EndGizmo(Gizmo);
    EndUI(UI);
}

extern "C" __declspec(dllexport)
GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state* GameState = (game_state*)Memory->PermanentStorage;
    GameOutputSound(GameState, SoundBuffer);
}
