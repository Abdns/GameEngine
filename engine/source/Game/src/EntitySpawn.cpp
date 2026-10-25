#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "GameState.h"

internal uint32 AddEntity(game_state *GameState, entity_type Type, transform Pose,
                          uint32 MeshHandle, uint32 MaterialHandle, const char *Name)
{
    world_position Position = MapIntoChunkSpace(GameState->World, WorldOrigin(), Pose.Position);
    uint32 StorageIndex = AddLowEntity(&GameState->Storage, Type, Name);
    if (StorageIndex == ENTITY_STORAGE_NONE) return ENTITY_STORAGE_NONE;

    ChangeEntityLocation(&GameState->WorldArena, GameState->World, &GameState->Storage, StorageIndex, Position);
    sim_entity *Entity = &GetLowEntity(&GameState->Storage, StorageIndex)->SimVariant;
    Entity->Flags = EntityFlag_Visible;
    Entity->Pose = Pose;
    Entity->MeshHandle = MeshHandle;
    Entity->MaterialHandle = MaterialHandle;
    Entity->AnimationHandle = ASSET_HANDLE_NONE;
    return StorageIndex;
}
