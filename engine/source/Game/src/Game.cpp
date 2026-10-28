#include "Game.h"

#include "AssetStore.cpp"
#include "SimEntity.cpp"
#include "World.cpp"
#include "EntityStorage.cpp"
#include "SimRegion.cpp"
#include "Input.cpp"
#include "Camera.cpp"
#include "Material.cpp"
#include "UI.cpp"
#include "Picking.cpp"
#include "Gizmo.cpp"

#include "GameState.h"

#include "EntitySpawn.cpp"
#include "EditorUI.cpp"

#define FRAME_ARENA_SIZE      Megabytes(8)
#define MATERIALS_ARENA_SIZE      Megabytes(1)
#define WORLD_CHUNK_DIM       16.0f
#define ENTITY_CAPACITY       4096
#define SIM_MAX_ENTITIES      4096
#define SIM_HALF_DIM          48.0f

internal void LoadAssetPack(const char *Name, game_memory *Memory, game_state *GameState)
{
    file_data PackFile = Memory->PlatformReadEntireFile(Name);
    AssetStoreLoadPack(&GameState->Assets, &GameState->WorldArena, PackFile.Data, PackFile.Size);
    Memory->PlatformFreeFileMemory(PackFile.Data);
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

        Vector4 Tint = Entity->Tint;
        if (Entity->LowStorageIndex == HighlightIndex)
        {
            Tint = Vector4(HighlightColor.X, HighlightColor.Y, HighlightColor.Z, Tint.W);
        }

        enga_vertex *PosedVertices = 0;
        if (Entity->AnimationHandle != ASSET_HANDLE_NONE)
        {
            PosedVertices = AssetPoseMesh(Assets, FrameArena, Entity->MeshHandle, Entity->AnimationHandle, Entity->AnimationTime);
        }

        PushRenderPosedMesh(Commands, SimEntityRenderTransform(Entity), Tint, Entity->MeshHandle, Entity->MaterialHandle, PosedVertices);
    }
}

internal void SelectEntity(game_state *GameState, sim_region *Region, ray PickRay)
{
    mouse_input *Mouse = &GameState->Controls.Mouse;
    if (!MouseAvailable(Mouse) || !Mouse->Pressed) return;

    Mouse->Consumed = true;
    uint32 Selected = PickEntity(PickRay, Region, &GameState->Assets);
    GizmoSelect(&GameState->Gizmo, Selected);
}

internal void InitTools(game_state *GameState)
{
    asset_store *Assets    = &GameState->Assets;
    materials   *Materials = GameState->Materials;

    uint32 AxisMeshes[GIZMO_AXIS_COUNT];
    AxisMeshes[0] = GetAssetMeshHandle(Assets, "gizmo_axis_x");
    AxisMeshes[1] = GetAssetMeshHandle(Assets, "gizmo_axis_y");
    AxisMeshes[2] = GetAssetMeshHandle(Assets, "gizmo_axis_z");

    /*uint32 GizmoMaterial = AddMaterial(Materials, OverlayMaterial());

    GameState->UI.Style = DefaultUIStyle();
    GameState->Gizmo.Style    = DefaultGizmoStyle(AxisMeshes, GizmoMaterial);
    GameState->Gizmo.Selected = ENTITY_STORAGE_NONE;*/

    //uint32 FontTexture = GetAssetTextureHandle(&GameState->Assets, "font_atlas");
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
    uint32 PropMeshes[] = { Cube, Sphere, Plane };

    auto Params = MatParams(TextureHandles, 1, Vector4(1, 1, 1, 1));
    material PropMaterial = UnlitMaterial(Materials, Params);

    AddEntity(GameState, Entity_Floor, TransformAt(Vector3(0.0f, -2.1f, 0.0f)), Plane, PropMaterial.Handle, "floor");
    AddEntity(GameState, Entity_Prop, TransformAt(Vector3(-2.5f, 0.0f, 0.0f)), Sphere, PropMaterial.Handle, 0);
    AddEntity(GameState, Entity_Prop, TransformAt(Vector3( 2.5f, 0.0f, 0.0f)), Cube, PropMaterial.Handle, 0);

}

internal void InitGame(game_memory *Memory, game_state *GameState, render_commands *RenderCommands)
{
    InitializeArena(&GameState->WorldArena, Memory->PermanentStorageSize - sizeof(game_state), (uint8 *)Memory->PermanentStorage + sizeof(game_state));

    memory_arena *WorldArena = &GameState->WorldArena;
    SubArena(&GameState->FrameArena, WorldArena, FRAME_ARENA_SIZE);
    SubArena(&GameState->MaterialsArena, WorldArena, MATERIALS_ARENA_SIZE);

    LoadAssetPack(ENGA_PACK_PATH, Memory, GameState);
    asset_store *Assets = &GameState->Assets;
    PushAssetsToRender(Assets, RenderCommands);

    GameState->World = PushStruct(WorldArena, world);
    GameState->Materials = PushStruct(WorldArena, materials);
    WorldInit(GameState->World, WORLD_CHUNK_DIM);
    InitMateriasls(*GameState->Materials, GameState->MaterialsArena);

    EntityStorageInit(&GameState->Storage, WorldArena, ENTITY_CAPACITY);

    InitTools(GameState);    
    InitCamera(&GameState->Camera, MapIntoChunkSpace(GameState->World, WorldOrigin(), Vector3(0.0f, 0.0f, 4.0f)), DegToRad(75.0f));
    
    BuildTestScene(GameState);
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
    gizmo_context *Gizmo    = &GameState->Gizmo;
    input_state   *Controls = &GameState->Controls;
    mouse_input   *Mouse    = &Controls->Mouse;

    BeginInput(Controls, Input);
    BeginUI(&GameState->UI, RenderCommands, Mouse, Input->UIScale);
    BeginGizmo(Gizmo, Mouse, RenderCommands);
    DebugUI(GameState);

    rectangle3  SimBounds = Rect3CenterRadius(Vector3(0.0f, 0.0f, 0.0f), SIM_HALF_DIM);
    sim_region *Region    = BeginSim(&GameState->FrameArena, GameState->World, &GameState->Storage, Camera->Position, SimBounds, SIM_MAX_ENTITIES);
    {
        UpdateCamera(Camera, GameState->World, Controls);

        Vector3 CameraSimP = WorldSubtract(GameState->World, &Camera->Position, &Region->Origin);

        ray PickRay = CameraRayFromScreen(Camera, CameraSimP, Mouse->Position.X, Mouse->Position.Y, Controls->ViewportSize.X, Controls->ViewportSize.Y);

        sim_entity *Selected = GetEntityByStorageIndex(Region, Gizmo->Selected);
        if (Selected)
        {
            Vector3 GizmoPosition = Selected->Pose.Position;

            if (Axis(Gizmo, &GameState->Assets, PickRay, &GizmoPosition))
            {
                Selected->Pose.Position = GizmoPosition;
            }
        }

        SelectEntity(GameState, Region, PickRay);

        Vector3 CameraWorldP = WorldPositionToMeters(GameState->World, Region->Origin) + CameraSimP;

        PushBindCamera(RenderCommands, CameraView(Camera, CameraSimP), CameraWorldP, Camera->FovY);
        PushBindLight(RenderCommands, Vector3(0.4f, 1.0f, 0.3f));
        PushRenderSkybox(RenderCommands, GameState->SkyHandle);
        AnimateEntities(Region, &GameState->Assets, Controls->dtForFrame);

        PushEntitiesToRender(Region, &GameState->Assets, &GameState->FrameArena, RenderCommands, Gizmo->Selected, Gizmo->Style.Selected);
    }
    EndSim(Region, &GameState->WorldArena);

    EndGizmo(Gizmo);
}

extern "C" __declspec(dllexport)
GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
}
