#include "Types.h"
#include "Intrinsics.h"
#include "EngineMath.h"
#include "Memory.h"
#include "PlatformAPI.h"

#include "AssetStore.cpp"
#include "SimEntity.cpp"
#include "World.cpp"
#include "EntityStorage.cpp"
#include "SimRegion.cpp"
#include "GameInput.cpp"
#include "Camera.cpp"
#include "Material.cpp"
#include "UI.cpp"
#include "Picking.cpp"
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

    game_input    Controls;
    ui_context    UI;
    gizmo_context Gizmo;

    uint32 Hero;
    world_position CameraPosition;

    uint8 debug;
};

#include "EntitySpawn.cpp"
#include "DebugEditor.cpp"

#define FRAME_ARENA_SIZE      Megabytes(8)
#define MATERIALS_ARENA_SIZE  Megabytes(1)
#define WORLD_CHUNK_DIM       16.0f
#define ENTITY_CAPACITY       4096
#define SIM_MAX_ENTITIES      4096
#define SIM_HALF_DIM          48.0f



internal void LoadAssetsFromEnga(const char* Name, game_memory* Memory, game_state* GameState)
{
    file_data EngaFile = Memory->PlatformReadEntireFile(Name);
    LoadAssetsFromEngaMemory(&GameState->Assets, &GameState->WorldArena, EngaFile.Data, EngaFile.Size);
    Memory->PlatformFreeFileMemory(EngaFile.Data);
}

internal void AnimateEntities(sim_region *Region, asset_store *Assets, real32 dt)
{
    for (uint32 Index = 0; Index < Region->EntityCount; ++Index)
    {
        sim_entity *Entity = &Region->Entities[Index];
        if (Entity->AnimationHandle == ASSET_HANDLE_NONE)
        {
            continue;
        }

        real32 Duration = Assets->Animations[Entity->AnimationHandle].Duration;

        Entity->AnimationTime += dt;
        while (Duration > 0.0f && Entity->AnimationTime >= Duration)
        {
            Entity->AnimationTime -= Duration;
        }
    }
}

internal void PushEntitiesToRender(sim_region *Region, asset_store *Assets, memory_arena *FrameArena, render_commands *Commands, uint32 HighlightIndex, Vector4 HighlightColor)
{
    for (uint32 Index = 0; Index < Region->EntityCount; ++Index)
    {
        sim_entity *Entity = &Region->Entities[Index];

        if (!(Entity->Flags & EntityFlag_Visible))
        {
            continue;
        }

        material_params Params = Entity->Params;
        if (Entity->LowStorageIndex == HighlightIndex)
        {
            Params.Color = Vector4(HighlightColor.X, HighlightColor.Y, HighlightColor.Z, Params.Color.W);
        }

        enga_vertex *PosedVertices = 0;
        if (Entity->AnimationHandle != ASSET_HANDLE_NONE)
        {
            PosedVertices = AssetPoseMesh(Assets, FrameArena, Entity->MeshHandle, Entity->AnimationHandle, Entity->AnimationTime);
        }

        PushRenderPosedMesh(Commands, SimEntityRenderTransform(Entity), Entity->MeshHandle, Params, PosedVertices);
    }
}

internal void BuildTestScene(game_state *GameState)
{
    asset_store *Assets    = &GameState->Assets;
    materials   *Materials = GameState->Materials;

    GameState->SkyHandle  = GetAssetCubemapHandle(Assets, "sky");

    uint32 TestTextureHandle = GetAssetTextureHandle(Assets, "TestTexture");
    uint32 TextureHandles[] = { TestTextureHandle };

    uint32 Cube = GetAssetMeshHandle(Assets, "cube");
    uint32 Sphere = GetAssetMeshHandle(Assets, "sphere");
    uint32 Plane = GetAssetMeshHandle(Assets, "plane");

    uint32 PropMaterial = UnlitMaterial(Materials);
    material_params Params = MatParams(PropMaterial, TextureHandles, 1, Vector4(1, 1, 1, 1));

    AddEntity(GameState, Entity_Floor, TransformAt(Vector3(0.0f, -2.1f, 0.0f)), Plane, Params, "floor");
    AddEntity(GameState, Entity_Prop, TransformAt(Vector3(-2.5f, 0.0f, 0.0f)), Sphere, Params, 0);
    AddEntity(GameState, Entity_Prop, TransformAt(Vector3( 2.5f, 0.0f, 0.0f)), Cube, Params, 0);
}

internal void BuildHero(game_state* GameState)
{
    asset_store* Assets = &GameState->Assets;
    materials* Materials = GameState->Materials;

    uint32 TestTextureHandle = GetAssetTextureHandle(Assets, "TestTexture");
    uint32 TextureHandles[] = { TestTextureHandle };

    uint32 TestMale = GetAssetMeshHandle(Assets, "TestMale");

    uint32 PropMaterial = LitMaterial(Materials);
    material_params Params = MatParams(PropMaterial, TextureHandles, 1, Vector4(1, 1, 1, 1));

    GameState->Hero = AddEntity(GameState, Entity_Prop, TransformAt(Vector3(2.5f, 0.0f, 0.0f)), TestMale, Params, 0);
}

internal void UpdateHero(game_state* GameState, game_input* Input)
{
    if (!GameState->debug)
    {
        Vector3 vec = Vector3(Input->MoveAxis.X, 0, -Input->MoveAxis.Z);

        low_entity *player = GetLowEntity(&GameState->Storage, GameState->Hero);
        world_position newPos = MapIntoChunkSpace(GameState->World, player->Position, vec * Input->dtForFrame * 4);
        ChangeEntityLocation(&GameState->WorldArena, GameState->World, &GameState->Storage, GameState->Hero, newPos);
    }
}

internal void UpdateENV(game_state* GameState, render_commands* RenderCommands)
{
    directional_light DirLight = {};
    DirLight.Color = Vector4(1, 1, 1, 1);
    DirLight.Direction = Vector3(0.4f, 1.0f, 0.3f);
    DirLight.Intensity = 1.0f;
    PushBindLight(RenderCommands, DirLight);
    PushRenderSkybox(RenderCommands, GameState->SkyHandle);
}

internal void InitGame(game_memory *Memory, game_state *GameState, render_commands *RenderCommands)
{
    InitializeArena(&GameState->WorldArena, Memory->PermanentStorageSize - sizeof(game_state), (uint8 *)Memory->PermanentStorage + sizeof(game_state));

    memory_arena *WorldArena = &GameState->WorldArena;
    SubArena(&GameState->FrameArena, WorldArena, FRAME_ARENA_SIZE);
    SubArena(&GameState->MaterialsArena, WorldArena, MATERIALS_ARENA_SIZE);

    LoadAssetsFromEnga(ENGA_PACK_PATH, Memory, GameState);
    asset_store *Assets = &GameState->Assets;
    PushAssetsToRender(Assets, RenderCommands);

    GameState->Materials = PushStruct(WorldArena, materials);
    GameState->World = PushStruct(WorldArena, world);
    WorldInit(GameState->World, WORLD_CHUNK_DIM);

    InitMaterials(GameState->Materials);

    EntityStorageInit(&GameState->Storage, WorldArena, ENTITY_CAPACITY);

    InitTools(GameState);    
    InitCamera(&GameState->Camera, MapIntoChunkSpace(GameState->World, WorldOrigin(), Vector3(0.0f, 4.0f, 4.0f)), Vector3(-45.0f, 0.0f, 0.0f), DegToRad(75.0f));
    
    BuildTestScene(GameState);
    BuildHero(GameState);
    PushMaterialsToRender(GameState->Materials, RenderCommands);
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
    game_input    *Controls = &GameState->Controls;

    BindInput(Controls, Input);
    GameState->debug = (uint8)IsDebugMode(Controls);

    UpdateHero(GameState, Controls);
    UpdateCamera(Camera, GameState->World, Controls);

    rectangle3  SimBounds = Rect3CenterRadius(Vector3(0.0f, 0.0f, 0.0f), SIM_HALF_DIM);
    sim_region *Region = BeginSim(&GameState->FrameArena, GameState->World, &GameState->Storage, Camera->Position, SimBounds, SIM_MAX_ENTITIES);
    {
        Vector3 CameraSimP = WorldSubtract(GameState->World, &Camera->Position, &Region->Origin);
        uint32 HighlightIndex = UpdateDebugEditor(GameState, Region, RenderCommands);

        Vector3 CameraWorldP = WorldPositionToMeters(GameState->World, Region->Origin) + CameraSimP;

        PushBindCamera(RenderCommands, CameraView(Camera, CameraSimP), CameraWorldP, Camera->FovY);
        AnimateEntities(Region, &GameState->Assets, Controls->dtForFrame);

        PushEntitiesToRender(Region, &GameState->Assets, &GameState->FrameArena, RenderCommands, HighlightIndex, GameState->Gizmo.Style.Selected);
    }
    EndSim(Region, &GameState->WorldArena);

    UpdateENV(GameState, RenderCommands);
}

extern "C" __declspec(dllexport)
GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
}
