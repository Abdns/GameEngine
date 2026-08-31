#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "GameState.h"

#define BALL_SPEED 15.0f

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

internal uint32 AddEntityFromPreset(game_state *GameState, uint32 PresetIndex, world_position Origin, Vector3 Offset)
{
    preset_table *Presets = &GameState->Presets;

    if (PresetIndex >= Presets->Count)
    {
        return ENTITY_STORAGE_NONE;
    }

    entity_preset *Preset = Presets->Presets + PresetIndex;

    transform Pose = Preset->Pose;
    Pose.Position  = Preset->Pose.Position + Offset;

    return AddEntityAt(GameState, (entity_type)Preset->Type, Origin, Pose, Presets->MeshHandles[PresetIndex], Presets->MaterialHandles[PresetIndex], Preset->Static, Preset->Name);
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
    uint32 PresetIndex = GetPresetIndex(&GameState->Presets, "ball");

    uint32 StorageIndex = AddEntityFromPreset(GameState, PresetIndex, Region->Origin, Aim.Origin + Aim.Direction);
    if (StorageIndex == ENTITY_STORAGE_NONE)
    {
        return;
    }

    low_entity *Stored = GetLowEntity(&GameState->Storage, StorageIndex);
    Stored->SimVariant.dPosition = Aim.Direction * BALL_SPEED;

    Vector3 SimSpaceP = GetSimSpacePosition(Region, Stored);
    AddEntityToRegion(Region, StorageIndex, Stored, SimSpaceP, SimSpaceP);
}
