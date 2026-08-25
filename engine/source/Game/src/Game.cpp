#include "Game.h"

#include "AssetStore.cpp"
#include "World.cpp"
#include "Entity.cpp"
#include "SimRegion.cpp"
#include "Shapes.cpp"
#include "Broadphase.cpp"
#include "Physics.cpp"
#include "Raycast.cpp"
#include "Camera.cpp"
#include "Material.cpp"
#include "Text.cpp"
#include "UI.cpp"

#include "GameState.h"

#define FRAME_ARENA_SIZE      Megabytes(8)
#define WORLD_CHUNK_DIM       16.0f
#define ENTITY_CAPACITY       4096
#define SIM_MAX_ENTITIES      4096
#define SIM_HALF_DIM          48.0f
#define BALL_SPEED            15.0f

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

internal uint32 AddEntityAt(game_state *GameState, entity_type Type, world_position Position, uint32 MeshHandle, uint32 MaterialHandle, bool32 Static, const char *Name)
{
    uint32 StorageIndex = AddLowEntity(&GameState->WorldArena, GameState->World, &GameState->Storage, Type, Position, Name);
    if (StorageIndex == ENTITY_STORAGE_NONE)
    {
        return ENTITY_STORAGE_NONE;
    }

    low_entity *Stored = GetLowEntity(&GameState->Storage, StorageIndex);

    Stored->SimVariant.Flags          = EntityFlag_Visible | EntityFlag_Simulates | (Static ? EntityFlag_Static : 0);
    Stored->SimVariant.MeshHandle     = MeshHandle;
    Stored->SimVariant.MaterialHandle = MaterialHandle;

    PhysicsSetMass(&GameState->Physics, &Stored->SimVariant, Static);

    return StorageIndex;
}

internal uint32 AddEntity(game_state *GameState, entity_type Type, Vector3 Offset, uint32 MeshHandle, uint32 MaterialHandle, bool32 Static, const char *Name)
{
    return AddEntityAt(GameState, Type, MapIntoChunkSpace(GameState->World, WorldOrigin(), Offset), MeshHandle, MaterialHandle, Static, Name);
}

internal void ClearSpawnedEntities(game_state *GameState)
{
    entity_storage *Storage = &GameState->Storage;

    for (uint32 StorageIndex = 1; StorageIndex < Storage->Count; ++StorageIndex)
    {
        low_entity *Stored = Storage->LowEntities + StorageIndex;

        if (Stored->SimVariant.Type == Entity_Null || (Stored->SimVariant.Flags & EntityFlag_Static))
        {
            continue;
        }

        RemoveLowEntity(&GameState->WorldArena, GameState->World, Storage, StorageIndex);
    }

    GameState->SelectedStorageIndex = ENTITY_STORAGE_NONE;
}

internal void ShootBall(game_state *GameState, sim_region *Region, ray Aim)
{
    world_position P = MapIntoChunkSpace(GameState->World, Region->Origin, Aim.Origin + Aim.Direction);

    uint32 StorageIndex = AddEntityAt(GameState, Entity_Ball, P, GameState->SpawnMeshHandles[1], GameState->SpawnMaterialHandles[0], false, 0);
    if (StorageIndex == ENTITY_STORAGE_NONE)
    {
        return;
    }

    low_entity *Stored = GetLowEntity(&GameState->Storage, StorageIndex);
    Stored->SimVariant.dPosition = Aim.Direction * BALL_SPEED;

    Vector3 SimSpaceP = GetSimSpacePosition(Region, Stored);
    AddEntityToRegion(Region, StorageIndex, Stored, SimSpaceP, SimSpaceP);
}

internal void PushEntitiesToRender(sim_region *Region, render_commands *Commands, uint32 SelectedStorageIndex, real32 Alpha)
{
    for (uint32 Index = 0; Index < Region->EntityCount; ++Index)
    {
        sim_entity *Entity = Region->Entities + Index;

        if (!(Entity->Flags & EntityFlag_Visible))
        {
            continue;
        }

        Vector4 Tint = Entity->Tint;
        if (Entity->StorageIndex == SelectedStorageIndex)
        {
            Tint = Vector4(1.0f, 0.85f, 0.2f, Tint.W);
        }

        PushRenderMesh(Commands, SimEntityRenderTransform(Entity, Alpha), Tint, Entity->MeshHandle, Entity->MaterialHandle);
    }
}

internal void InitGame(game_memory *Memory, game_state *GameState, render_commands *RenderCommands)
{
    GameState->tSine = 0.0f;

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

    uint32 TexTestHandle = AssetTextureHandle(Assets, "test");

    GameState->SkyHandle  = AssetCubemapHandle(Assets, "sky");
    GameState->FontHandle = AssetFontHandle(Assets, "DejaVuSansMono24");

    GameState->SpawnMeshHandles[0] = AssetMeshHandle(Assets, "cube");
    GameState->SpawnMeshHandles[1] = AssetMeshHandle(Assets, "sphere");

    materials *Materials = &GameState->Materials;
    GameState->SpawnMaterialHandles[0] = AddMaterial(Materials, UnlitMaterial(Vector4(1.0f, 1.0f, 1.0f, 1.0f), TexTestHandle));
    GameState->SpawnMaterialHandles[1] = GameState->SpawnMaterialHandles[0];
    GameState->SpawnMaterialHandles[2] = GameState->SpawnMaterialHandles[0];

    uint32 LitMaterialHandle   = AddMaterial(Materials, LitMaterial(Vector4(0.9f, 0.5f, 0.2f, 1.0f), 1.0f, 0.25f));
    uint32 FloorMaterialHandle = AddMaterial(Materials, LitMaterial(Vector4(0.45f, 0.45f, 0.5f, 1.0f), 0.0f, 0.7f));

    for (uint32 Row = 0; Row < 2; ++Row)
    {
        real32 Metallic = (real32)Row;

        for (uint32 Column = 0; Column < 7; ++Column)
        {
            real32 Roughness = 0.05f + (real32)Column * 0.15f;

            uint32 BallMaterial = AddMaterial(Materials, LitMaterial(Vector4(0.75f, 0.05f, 0.05f, 1.0f), Metallic, Roughness));

            Vector3 Position = Vector3(((real32)Column - 3.0f) * 1.3f, Metallic * 1.6f - 0.8f, -4.0f);

            AddEntity(GameState, Entity_Prop, Position, GameState->SpawnMeshHandles[1], BallMaterial, true, 0);
        }
    }

    PushMaterialsToRender(Materials, RenderCommands);

    AddEntity(GameState, Entity_Floor, Vector3(0.0f, -2.1f, 0.0f), AssetMeshHandle(Assets, "plane"), FloorMaterialHandle, true, "floor");

    for (uint32 SpawnIndex = 0; SpawnIndex < 3; ++SpawnIndex)
    {
        real32 Angle  = (real32)SpawnIndex * 2.39996f;
        real32 Radius = 0.9f * SquareRoot((real32)SpawnIndex + 1.0f);

        Vector3 Position = Vector3(Cos(Angle) * Radius, 0.0f, Sin(Angle) * Radius);

        uint32 MeshHandle     = GameState->SpawnMeshHandles[SpawnIndex % ArrayCount(GameState->SpawnMeshHandles)];
        uint32 MaterialHandle = GameState->SpawnMaterialHandles[SpawnIndex % ArrayCount(GameState->SpawnMaterialHandles)];

        AddEntity(GameState, Entity_Prop, Position, MeshHandle, MaterialHandle, false, 0);
    }

    AddEntity(GameState, Entity_Prop, Vector3(-2.5f, 0.0f, 0.0f), GameState->SpawnMeshHandles[1], LitMaterialHandle, false, 0);
    AddEntity(GameState, Entity_Prop, Vector3( 2.5f, 0.0f, 0.0f), GameState->SpawnMeshHandles[0], LitMaterialHandle, false, 0);

    InitCamera(&GameState->Camera, MapIntoChunkSpace(GameState->World, WorldOrigin(), Vector3(0.0f, 0.0f, 4.0f)), DegToRad(75.0f));

    GameState->SelectedStorageIndex = ENTITY_STORAGE_NONE;

    GameState->UI.Style = DefaultUIStyle();
    GameState->Paused   = false;

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

    asset_store *Assets = &GameState->Assets;
    camera      *Camera = &GameState->Camera;
    ui_context  *UI     = &GameState->UI;

    BeginUI(UI, Input, RenderCommands);

    if (UILabeledButton(UI, Assets, GameState->FontHandle, "pause", RectMinDim(20.0f, 20.0f, 140.0f, 36.0f)))
    {
        GameState->Paused = !GameState->Paused;
    }

    if (UILabeledButton(UI, Assets, GameState->FontHandle, "spawn", RectMinDim(20.0f, 66.0f, 140.0f, 36.0f)))
    {
        AddEntity(GameState, Entity_Prop, Vector3(0.0f, 3.0f, 0.0f), GameState->SpawnMeshHandles[0], GameState->SpawnMaterialHandles[0], false, 0);
    }

    if (UILabeledButton(UI, Assets, GameState->FontHandle, "clear", RectMinDim(20.0f, 112.0f, 140.0f, 36.0f)))
    {
        ClearSpawnedEntities(GameState);
    }

    rectangle3  SimBounds = Rect3CenterRadius(Vector3(0.0f, 0.0f, 0.0f), SIM_HALF_DIM);
    sim_region *Region    = BeginSim(&GameState->FrameArena, GameState->World, &GameState->Storage, Camera->Position, SimBounds, SIM_MAX_ENTITIES);
    {
        PhysicsAccumulate(&GameState->Physics, Input->dtForFrame);

        while (PhysicsNextTick(&GameState->Physics))
        {
            SimSavePreviousTransforms(Region);

            if (!GameState->Paused)
            {
                PhysicsStep(&GameState->Physics, Region);
            }
        }

        real32 RenderAlpha = PhysicsRenderAlpha(&GameState->Physics);

        UpdateCamera(Camera, GameState->World, Input);

        Vector3 CameraSimP = WorldSubtract(GameState->World, &Camera->Position, &Region->Origin);

        bool32 PickedThisFrame = (UI->MousePressed && !UI->Active);
        if (PickedThisFrame && Input->RenderWidth > 0 && Input->RenderHeight > 0)
        {
            ray PickRay = CameraRayFromScreen(Camera, CameraSimP, (real32)Input->MouseX, (real32)Input->MouseY, (real32)Input->RenderWidth, (real32)Input->RenderHeight);

            raycast_hit Hit;
            GameState->SelectedStorageIndex = RayCastSim(PickRay, Region, Assets, &GameState->FrameArena, RenderAlpha, &Hit) ? Hit.StorageIndex : ENTITY_STORAGE_NONE;

            ShootBall(GameState, Region, PickRay);
        }

        PushRenderCamera(RenderCommands, CameraView(Camera, CameraSimP), CameraSimP, Camera->FovY);
        PushRenderLight(RenderCommands, Vector3(0.4f, 1.0f, 0.3f), Vector3(3.0f, 2.85f, 2.6f));
        PushRenderSkybox(RenderCommands, GameState->SkyHandle);
        PushEntitiesToRender(Region, RenderCommands, GameState->SelectedStorageIndex, RenderAlpha);

    }
    EndSim(Region, &GameState->WorldArena);

    EndUI(UI);
}

extern "C" __declspec(dllexport)
GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state* GameState = (game_state*)Memory->PermanentStorage;
    GameOutputSound(GameState, SoundBuffer);
}
