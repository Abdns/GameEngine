#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "GameState.h"

#define BALL_SPEED 15.0f
#define MAX_ENTITY_PRESETS 10

struct entity_preset
{
    const char* Name;
    entity_type Type;
    uint32      MeshHandle;
    uint32      MaterialHandle;
    transform   Pose;
    bool32      Static;
};

entity_preset Presets[MAX_ENTITY_PRESETS];
uint32        PresetCount;


internal uint32 AddEntityAt(game_state *GameState, entity_type Type, world_position Origin, transform Pose, uint32 MeshHandle, uint32 MaterialHandle, bool32 Static, const char *Name)
{
    world_position Position = MapIntoChunkSpace(GameState->World, Origin, Pose.Position);

    uint32 StorageIndex = AddLowEntity(&GameState->Storage, Type, Position, Name);
    if (StorageIndex == ENTITY_STORAGE_NONE)
    {
        return ENTITY_STORAGE_NONE;
    }

    ChangeEntityLocation(&GameState->WorldArena, GameState->World, &GameState->Storage, StorageIndex, Position);

    low_entity *Stored = GetLowEntity(&GameState->Storage, StorageIndex);

    Stored->SimVariant.Flags          = EntityFlag_Visible | EntityFlag_Simulates | (Static ? EntityFlag_Static : 0);
    Stored->SimVariant.MeshHandle     = MeshHandle;
    Stored->SimVariant.MaterialHandle = MaterialHandle;

    SimEntitySetOrientation(&Stored->SimVariant, Pose.Orientation);
    SimEntitySetScale(&Stored->SimVariant, Pose.Scale);

    PhysicsSetMass(&GameState->Physics, &Stored->SimVariant, Static);

    return StorageIndex;
}

internal uint32 AddEntity(game_state *GameState, entity_type Type, transform Pose, uint32 MeshHandle, uint32 MaterialHandle, bool32 Static, const char *Name)
{
    return AddEntityAt(GameState, Type, WorldOrigin(), Pose, MeshHandle, MaterialHandle, Static, Name);
}

internal uint32 AddEntityFromPreset(game_state* GameState, entity_preset* Preset, world_position Origin, Vector3 Offset)
{
    transform Pose = Preset->Pose;
    Pose.Position = Offset;

    return AddEntityAt(GameState, Preset->Type, Origin, Pose, Preset->MeshHandle, Preset->MaterialHandle, Preset->Static, Preset->Name);
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

        ChangeEntityLocation(&GameState->WorldArena, GameState->World, Storage, StorageIndex, NullWorldPosition());

        RemoveLowEntity(Storage, StorageIndex);
    }

    GameState->Gizmo.Selected = ENTITY_STORAGE_NONE;
}

internal void ShootBall(game_state *GameState, sim_region *Region, ray Aim)
{
    transform Pose = TransformAt(Aim.Origin + Aim.Direction);

    uint32 StorageIndex = AddEntityAt(GameState, Entity_Ball, Region->Origin, Pose, GameState->SpawnMeshHandles[1], GameState->SpawnMaterialHandles[0], false, 0);
    if (StorageIndex == ENTITY_STORAGE_NONE)
    {
        return;
    }

    low_entity *Stored = GetLowEntity(&GameState->Storage, StorageIndex);
    Stored->SimVariant.dPosition = Aim.Direction * BALL_SPEED;

    Vector3 SimSpaceP = GetSimSpacePosition(Region, Stored);
    AddEntityToRegion(Region, StorageIndex, Stored, SimSpaceP, SimSpaceP);
}
