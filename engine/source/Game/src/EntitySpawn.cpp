#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "GameState.h"

internal uint32 AddEntity(game_state *GameState, entity_type Type, transform Pose,
                          uint32 MeshHandle, material_params Params, const char *Name)
{
    if (Params.SharedMatIndex >= GameState->Materials->Count || (Params.TextureCount && !Params.TextureHandles))
    {
        Assert(!"Invalid entity material parameters");
        return ENTITY_STORAGE_NONE;
    }

    memory_arena *Arena = &GameState->MaterialsArena;
    memory_size TextureSize = (memory_size)Params.TextureCount * sizeof(uint32);
    if (Params.TextureCount && TextureSize + GetAlignmentOffset(Arena, alignof(uint32)) > Arena->Size - Arena->Used)
    {
        Assert(!"Material parameter arena is full");
        return ENTITY_STORAGE_NONE;
    }

    world_position Position = MapIntoChunkSpace(GameState->World, WorldOrigin(), Pose.Position);
    uint32 StorageIndex = AddLowEntity(&GameState->Storage, Type, Name);
    if (StorageIndex == ENTITY_STORAGE_NONE) return ENTITY_STORAGE_NONE;

    ChangeEntityLocation(&GameState->WorldArena, GameState->World, &GameState->Storage, StorageIndex, Position);
    sim_entity *Entity = &GetLowEntity(&GameState->Storage, StorageIndex)->SimVariant;
    Entity->Flags = EntityFlag_Visible;
    Entity->Pose = Pose;
    Entity->MeshHandle = MeshHandle;
    Entity->Params = Params;
    Entity->Params.TextureHandles = 0;
    if (Params.TextureCount)
    {
        // Entity parameters outlive the caller's local texture array.
        uint32 *TextureHandles = PushArrayAligned(Arena, Params.TextureCount, uint32, alignof(uint32));
        for (uint32 Index = 0; Index < Params.TextureCount; ++Index)
        {
            TextureHandles[Index] = Params.TextureHandles[Index];
        }
        Entity->Params.TextureHandles = TextureHandles;
    }
    Entity->AnimationHandle = ASSET_HANDLE_NONE;
    return StorageIndex;
}
