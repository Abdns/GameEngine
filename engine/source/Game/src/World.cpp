#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#define WORLD_CHUNK_HASH_SIZE        4096
#define WORLD_ENTITIES_PER_BLOCK     16
#define WORLD_CHUNK_UNINITIALIZED    INT32_MAX

struct world_position
{
    int32   ChunkX;
    int32   ChunkY;
    int32   ChunkZ;

    Vector3 Offset;
};

struct world_entity_block
{
    uint32              EntityCount;
    uint32              StorageIndex[WORLD_ENTITIES_PER_BLOCK];
    world_entity_block *Next;
};

struct world_chunk
{
    int32 ChunkX;
    int32 ChunkY;
    int32 ChunkZ;

    world_entity_block  FirstBlock;
    world_chunk        *NextInHash;
};

struct world
{
    real32 ChunkDimInMeters;
    real32 InvChunkDimInMeters;

    world_chunk         ChunkHash[WORLD_CHUNK_HASH_SIZE];
    world_entity_block *FirstFreeBlock;
};

internal void WorldInit(world *World, real32 ChunkDimInMeters)
{
    World->ChunkDimInMeters    = ChunkDimInMeters;
    World->InvChunkDimInMeters = 1.0f / ChunkDimInMeters;
    World->FirstFreeBlock      = 0;

    for (uint32 Index = 0; Index < ArrayCount(World->ChunkHash); ++Index)
    {
        World->ChunkHash[Index].ChunkX                = WORLD_CHUNK_UNINITIALIZED;
        World->ChunkHash[Index].FirstBlock.EntityCount = 0;
        World->ChunkHash[Index].FirstBlock.Next        = 0;
        World->ChunkHash[Index].NextInHash             = 0;
    }
}

inline world_position NullWorldPosition(void)
{
    world_position Result = {};
    Result.ChunkX = WORLD_CHUNK_UNINITIALIZED;

    return Result;
}

inline bool32 IsWorldPositionValid(world_position P)
{
    return P.ChunkX != WORLD_CHUNK_UNINITIALIZED;
}

inline bool32 AreInSameChunk(world_position *A, world_position *B)
{
    return A->ChunkX == B->ChunkX && A->ChunkY == B->ChunkY && A->ChunkZ == B->ChunkZ;
}

inline void RecanonicalizeCoord(world *World, int32 *Chunk, real32 *Coord)
{
    int32 Offset = FloorReal32ToInt32(*Coord * World->InvChunkDimInMeters + 0.5f);

    *Chunk += Offset;
    *Coord -= (real32)Offset * World->ChunkDimInMeters;
}

inline world_position MapIntoChunkSpace(world *World, world_position Base, Vector3 Offset)
{
    world_position Result = Base;

    Result.Offset += Offset;

    RecanonicalizeCoord(World, &Result.ChunkX, &Result.Offset.X);
    RecanonicalizeCoord(World, &Result.ChunkY, &Result.Offset.Y);
    RecanonicalizeCoord(World, &Result.ChunkZ, &Result.Offset.Z);

    return Result;
}

inline world_position WorldPositionFromChunk(int32 ChunkX, int32 ChunkY, int32 ChunkZ, Vector3 Offset)
{
    world_position Result = {};

    Result.ChunkX = ChunkX;
    Result.ChunkY = ChunkY;
    Result.ChunkZ = ChunkZ;
    Result.Offset = Offset;

    return Result;
}

inline Vector3 WorldPositionToMeters(world *World, world_position P)
{
    return Vector3((real32)P.ChunkX, (real32)P.ChunkY, (real32)P.ChunkZ) * World->ChunkDimInMeters + P.Offset;
}

inline Vector3 WorldSubtract(world *World, world_position *A, world_position *B)
{
    Vector3 ChunkDelta = Vector3((real32)(A->ChunkX - B->ChunkX), (real32)(A->ChunkY - B->ChunkY), (real32)(A->ChunkZ - B->ChunkZ));

    return World->ChunkDimInMeters * ChunkDelta + (A->Offset - B->Offset);
}

internal world_chunk *GetWorldChunk(world *World, int32 ChunkX, int32 ChunkY, int32 ChunkZ, memory_arena *Arena)
{
    uint32 HashValue = (uint32)(19 * ChunkX + 7 * ChunkY + 3 * ChunkZ);
    uint32 HashSlot  = HashValue & (ArrayCount(World->ChunkHash) - 1);

    world_chunk *Chunk = World->ChunkHash + HashSlot;

    while (Chunk)
    {
        if (Chunk->ChunkX == ChunkX && Chunk->ChunkY == ChunkY && Chunk->ChunkZ == ChunkZ)
        {
            break;
        }

        if (Arena && Chunk->ChunkX != WORLD_CHUNK_UNINITIALIZED && !Chunk->NextInHash)
        {
            Chunk->NextInHash = PushStruct(Arena, world_chunk);
            Chunk->NextInHash->ChunkX = WORLD_CHUNK_UNINITIALIZED;
        }

        if (Chunk->ChunkX == WORLD_CHUNK_UNINITIALIZED)
        {
            if (!Arena)
            {
                return 0;
            }

            Chunk->ChunkX = ChunkX;
            Chunk->ChunkY = ChunkY;
            Chunk->ChunkZ = ChunkZ;

            Chunk->FirstBlock.EntityCount = 0;
            Chunk->FirstBlock.Next = 0;
            Chunk->NextInHash = 0;

            break;
        }

        Chunk = Chunk->NextInHash;
    }

    return Chunk;
}

internal void WorldRemoveFromChunk(world *World, uint32 StorageIndex, world_position *P)
{
    world_chunk *Chunk = GetWorldChunk(World, P->ChunkX, P->ChunkY, P->ChunkZ, 0);
    if (!Chunk)
    {
        return;
    }

    world_entity_block *FirstBlock = &Chunk->FirstBlock;
    bool32 Found = false;

    for (world_entity_block *Block = FirstBlock; Block && !Found; Block = Block->Next)
    {
        for (uint32 Index = 0; Index < Block->EntityCount; ++Index)
        {
            if (Block->StorageIndex[Index] != StorageIndex)
            {
                continue;
            }

            Assert(FirstBlock->EntityCount > 0);
            Block->StorageIndex[Index] = FirstBlock->StorageIndex[--FirstBlock->EntityCount];

            if (FirstBlock->EntityCount == 0 && FirstBlock->Next)
            {
                world_entity_block *NextBlock = FirstBlock->Next;

                *FirstBlock = *NextBlock;

                NextBlock->Next       = World->FirstFreeBlock;
                World->FirstFreeBlock = NextBlock;
            }

            Found = true;
            break;
        }
    }
}

internal void WorldInsertIntoChunk(memory_arena *Arena, world *World, uint32 StorageIndex, world_position *P)
{
    world_chunk *Chunk = GetWorldChunk(World, P->ChunkX, P->ChunkY, P->ChunkZ, Arena);
    Assert(Chunk);

    world_entity_block *Block = &Chunk->FirstBlock;

    if (Block->EntityCount == ArrayCount(Block->StorageIndex))
    {
        world_entity_block *Spilled = World->FirstFreeBlock;

        if (Spilled)
        {
            World->FirstFreeBlock = Spilled->Next;
        }
        else
        {
            Spilled = PushStruct(Arena, world_entity_block);
        }

        *Spilled = *Block;

        Block->Next        = Spilled;
        Block->EntityCount = 0;
    }

    Block->StorageIndex[Block->EntityCount++] = StorageIndex;
}

internal void ChangeEntityLocationRaw(memory_arena *Arena, world *World, uint32 StorageIndex, world_position *OldP, world_position *NewP)
{
    if (OldP && NewP && AreInSameChunk(OldP, NewP))
    {
        return;
    }

    if (OldP)
    {
        WorldRemoveFromChunk(World, StorageIndex, OldP);
    }

    if (NewP)
    {
        WorldInsertIntoChunk(Arena, World, StorageIndex, NewP);
    }
}

internal uint32 WorldChunkEntityCount(world *World, int32 ChunkX, int32 ChunkY, int32 ChunkZ)
{
    world_chunk *Chunk = GetWorldChunk(World, ChunkX, ChunkY, ChunkZ, 0);
    if (!Chunk)
    {
        return 0;
    }

    uint32 Count = 0;
    for (world_entity_block *Block = &Chunk->FirstBlock; Block; Block = Block->Next)
    {
        Count += Block->EntityCount;
    }

    return Count;
}

inline world_position WorldOrigin(void)
{
    return WorldPositionFromChunk(0, 0, 0, Vector3(0.0f, 0.0f, 0.0f));
}
