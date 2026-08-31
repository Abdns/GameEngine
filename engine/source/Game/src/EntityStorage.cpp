#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#include "Strings.h"
#include "SimEntity.cpp"
#include "World.cpp"

#define ENTITY_MAX_NAME 32

struct low_entity
{
    world_position Position;
    world_position PrevPosition;

    sim_entity     SimVariant;

    char Name[ENTITY_MAX_NAME];
};

struct entity_storage
{
    uint32      Count;
    uint32      Capacity;
    low_entity *LowEntities;

    uint32     *FreeIndices;
    uint32      FreeCount;
};

internal void AddSimEntity(low_entity *Entity, uint32 StorageIndex, entity_type Type)
{
    Entity->SimVariant.LowStorageIndex = StorageIndex;
    Entity->SimVariant.Type            = Type;
    Entity->SimVariant.Current         = TransformIdentity();
    Entity->SimVariant.Previous        = TransformIdentity();
    Entity->SimVariant.Tint            = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
}

internal void EntityStorageInit(entity_storage *Storage, memory_arena *Arena, uint32 Capacity)
{
    Storage->Capacity  = Capacity;
    Storage->Count     = 1;
    Storage->FreeCount = 0;

    Storage->LowEntities = PushArray(Arena, Capacity, low_entity);
    Storage->FreeIndices = PushArray(Arena, Capacity, uint32);

    ZeroStruct(Storage->LowEntities[ENTITY_STORAGE_NONE]);
    Storage->LowEntities[ENTITY_STORAGE_NONE].Position = NullWorldPosition();
}

internal low_entity *GetLowEntity(entity_storage *Storage, uint32 StorageIndex)
{
    if (StorageIndex == ENTITY_STORAGE_NONE || StorageIndex >= Storage->Count)
    {
        return 0;
    }

    return Storage->LowEntities + StorageIndex;
}

internal char *LowEntityName(entity_storage *Storage, uint32 StorageIndex)
{
    Assert(StorageIndex < Storage->Capacity);

    return Storage->LowEntities[StorageIndex].Name;
}

internal uint32 FindLowEntityByName(entity_storage *Storage, const char *Name)
{
    Assert(Name && Name[0]);

    for (uint32 Index = 1; Index < Storage->Count; ++Index)
    {
        if (Storage->LowEntities[Index].SimVariant.Type != Entity_Null && StringsAreEqual(Storage->LowEntities[Index].Name, Name))
        {
            return Index;
        }
    }

    return ENTITY_STORAGE_NONE;
}

internal void ChangeEntityLocation(memory_arena *Arena, world *World, entity_storage *Storage, uint32 StorageIndex, world_position NewP)
{
    low_entity *Entity = Storage->LowEntities + StorageIndex;

    world_position *OldP  = IsWorldPositionValid(Entity->Position) ? &Entity->Position : 0;
    world_position *NextP = IsWorldPositionValid(NewP) ? &NewP : 0;

    ChangeEntityLocationRaw(Arena, World, StorageIndex, OldP, NextP);

    Entity->Position = NewP;
}

internal uint32 AddLowEntity(entity_storage *Storage, entity_type Type, world_position Position, const char *Name)
{
    uint32 StorageIndex;

    if (Storage->FreeCount)
    {
        StorageIndex = Storage->FreeIndices[--Storage->FreeCount];
    }
    else
    {
        if (Storage->Count >= Storage->Capacity)
        {
            DebugLog("Entity storage is full (%u entities)\n", Storage->Capacity);

            return ENTITY_STORAGE_NONE;
        }

        StorageIndex = Storage->Count++;
    }

    low_entity *Entity = Storage->LowEntities + StorageIndex;

    ZeroStruct(*Entity);

    Entity->Position     = NullWorldPosition();
    Entity->PrevPosition = Position;
    Entity->Name[0]      = 0;

    if (Name)
    {
        AppendString(Entity->Name, ENTITY_MAX_NAME, 0, Name);
    }

    AddSimEntity(Entity, StorageIndex, Type);

    return StorageIndex;
}

internal void RemoveLowEntity(entity_storage *Storage, uint32 StorageIndex)
{
    low_entity *Entity = GetLowEntity(Storage, StorageIndex);
    if (!Entity || Entity->SimVariant.Type == Entity_Null)
    {
        return;
    }

    Entity->SimVariant.Type  = Entity_Null;
    Entity->SimVariant.Flags = 0;
    Entity->Name[0]          = 0;

    Assert(Storage->FreeCount < Storage->Capacity);
    Storage->FreeIndices[Storage->FreeCount++] = StorageIndex;
}
