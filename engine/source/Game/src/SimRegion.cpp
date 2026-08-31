#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#include "EntityStorage.cpp"

#define SIM_HASH_SIZE           4096
#define SIM_MAX_ENTITY_RADIUS   16.0f

struct sim_entity_slot
{
    sim_entity *Ptr;
    uint32      StorageIndex;
};

struct sim_region
{
    world          *World;
    entity_storage *Storage;

    world_position Origin;
    rectangle3     Bounds;
    rectangle3     UpdatableBounds;

    uint32      MaxEntityCount;
    uint32      EntityCount;
    sim_entity *Entities;

    sim_entity_slot HashTable[SIM_HASH_SIZE];
};

internal sim_entity_slot *GetSlotFromStorageIndex(sim_region *Region, uint32 StorageIndex)
{
    Assert(StorageIndex != ENTITY_STORAGE_NONE);

    uint32 HashMask = ArrayCount(Region->HashTable) - 1;

    for (uint32 Offset = 0; Offset < ArrayCount(Region->HashTable); ++Offset)
    {
        sim_entity_slot *Entry = Region->HashTable + ((StorageIndex + Offset) & HashMask);

        if (Entry->StorageIndex == ENTITY_STORAGE_NONE || Entry->StorageIndex == StorageIndex)
        {
            return Entry;
        }
    }

    return 0;
}

internal sim_entity *GetEntityByStorageIndex(sim_region *Region, uint32 StorageIndex)
{
    if (StorageIndex == ENTITY_STORAGE_NONE)
    {
        return 0;
    }

    sim_entity_slot *Entry = GetSlotFromStorageIndex(Region, StorageIndex);

    return (Entry && Entry->StorageIndex == StorageIndex) ? Entry->Ptr : 0;
}

internal Vector3 GetSimSpacePosition(sim_region *Region, low_entity *Stored)
{
    return WorldSubtract(Region->World, &Stored->Position, &Region->Origin);
}

internal sim_entity *AddEntityRaw(sim_region *Region, uint32 StorageIndex, low_entity *Source)
{
    Assert(StorageIndex != ENTITY_STORAGE_NONE);

    sim_entity_slot *Entry = GetSlotFromStorageIndex(Region, StorageIndex);
    if (!Entry)
    {
        return 0;
    }

    if (Entry->Ptr)
    {
        return Entry->Ptr;
    }

    if (Region->EntityCount >= Region->MaxEntityCount)
    {
        DebugLog("Sim region is full (%u entities)\n", Region->MaxEntityCount);
        return 0;
    }

    sim_entity *Entity = Region->Entities + Region->EntityCount++;

    Entry->StorageIndex = StorageIndex;
    Entry->Ptr          = Entity;

    if (Source)
    {
        *Entity = Source->SimVariant;
    }
    else
    {
        ZeroStruct(*Entity);
    }

    Entity->LowStorageIndex = StorageIndex;
    Entity->Updatable    = false;

    return Entity;
}

internal sim_entity *AddEntityToRegion(sim_region *Region, uint32 StorageIndex, low_entity *Source, Vector3 SimSpaceP, Vector3 SimSpacePrevP)
{
    sim_entity *Entity = AddEntityRaw(Region, StorageIndex, Source);

    if (Entity)
    {
        Entity->Current.Position         = SimSpaceP;
        Entity->Previous.Position     = SimSpacePrevP;
        Entity->Updatable = Rect3Contains(Region->UpdatableBounds, SimSpaceP);
    }

    return Entity;
}

internal void LoadEntityReference(sim_region *Region, sim_entity_reference *Reference)
{
    if (Reference->Index == ENTITY_STORAGE_NONE)
    {
        Reference->Ptr = 0;
        return;
    }

    uint32      StorageIndex = Reference->Index;
    sim_entity *Entity       = GetEntityByStorageIndex(Region, StorageIndex);

    if (!Entity)
    {
        low_entity *Stored = GetLowEntity(Region->Storage, StorageIndex);
        if (Stored && Stored->SimVariant.Type != Entity_Null)
        {
            Entity = AddEntityRaw(Region, StorageIndex, Stored);
            if (Entity)
            {
                Entity->Current.Position     = GetSimSpacePosition(Region, Stored);
                Entity->Previous.Position = WorldSubtract(Region->World, &Stored->PrevPosition, &Region->Origin);
            }
        }
    }

    Reference->Ptr = Entity;
}

internal void StoreEntityReference(sim_entity_reference *Reference)
{
    Reference->Index = Reference->Ptr ? Reference->Ptr->LowStorageIndex : ENTITY_STORAGE_NONE;
}

internal sim_region *BeginSim(memory_arena *SimArena, world *World, entity_storage *Storage, world_position Origin, rectangle3 UpdatableBounds, uint32 MaxEntityCount)
{
    sim_region *Region = PushStruct(SimArena, sim_region);
    ZeroArray(ArrayCount(Region->HashTable), Region->HashTable);

    Region->World   = World;
    Region->Storage = Storage;
    Region->Origin  = Origin;

    Region->Bounds          = Rect3AddRadius(UpdatableBounds, SIM_MAX_ENTITY_RADIUS);
    Region->UpdatableBounds = UpdatableBounds;

    Region->MaxEntityCount = MaxEntityCount;
    Region->EntityCount    = 0;
    Region->Entities       = PushArray(SimArena, MaxEntityCount, sim_entity);

    world_position MinChunk = MapIntoChunkSpace(World, Origin, Region->Bounds.Min);
    world_position MaxChunk = MapIntoChunkSpace(World, Origin, Region->Bounds.Max);

    for (int32 ChunkZ = MinChunk.ChunkZ; ChunkZ <= MaxChunk.ChunkZ; ++ChunkZ)
    {
        for (int32 ChunkY = MinChunk.ChunkY; ChunkY <= MaxChunk.ChunkY; ++ChunkY)
        {
            for (int32 ChunkX = MinChunk.ChunkX; ChunkX <= MaxChunk.ChunkX; ++ChunkX)
            {
                world_chunk *Chunk = GetWorldChunk(World, ChunkX, ChunkY, ChunkZ, 0);
                if (!Chunk)
                {
                    continue;
                }

                for (world_entity_block *Block = &Chunk->FirstBlock; Block; Block = Block->Next)
                {
                    for (uint32 Index = 0; Index < Block->EntityCount; ++Index)
                    {
                        uint32      StorageIndex = Block->StorageIndex[Index];
                        low_entity *Stored       = Storage->LowEntities + StorageIndex;

                        if (Stored->SimVariant.Type == Entity_Null)
                        {
                            continue;
                        }

                        Vector3 SimSpaceP = GetSimSpacePosition(Region, Stored);
                        if (!Rect3Contains(Region->Bounds, SimSpaceP))
                        {
                            continue;
                        }

                        Vector3 SimSpacePrevP = WorldSubtract(World, &Stored->PrevPosition, &Region->Origin);

                        AddEntityToRegion(Region, StorageIndex, Stored, SimSpaceP, SimSpacePrevP);
                    }
                }
            }
        }
    }

    return Region;
}

internal void EndSim(sim_region *Region, memory_arena *Arena)
{
    world          *World   = Region->World;
    entity_storage *Storage = Region->Storage;

    for (uint32 Index = 0; Index < Region->EntityCount; ++Index)
    {
        sim_entity *Entity = Region->Entities + Index;
        low_entity *Stored = Storage->LowEntities + Entity->LowStorageIndex;

        Stored->SimVariant   = *Entity;
        Stored->PrevPosition = MapIntoChunkSpace(World, Region->Origin, Entity->Previous.Position);

        ChangeEntityLocation(Arena, World, Storage, Entity->LowStorageIndex, MapIntoChunkSpace(World, Region->Origin, Entity->Current.Position));
    }
}
