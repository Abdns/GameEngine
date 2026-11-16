#ifndef MEMORY_H
#define MEMORY_H

#include "Types.h"

#define DEFAULT_ALIGNMENT 4
typedef uint64 memory_size;

struct memory_arena
{
    uint8 *Base;
    memory_size Size;
    memory_size Used;
    int32 TempCount;
};

internal void InitializeArena(memory_arena *Arena, memory_size Size, void *Base)
{
    Arena->Size = Size;
    Arena->Base = (uint8 *)Base;
    Arena->Used = 0;
    Arena->TempCount = 0;
}

internal memory_size GetAlignmentOffset(memory_arena *Arena, memory_size Alignment)
{
    memory_size AlignmentOffset = 0;
    memory_size ResultPointer = (memory_size)Arena->Base + Arena->Used;
    memory_size AlignmentMask = Alignment - 1;
    if (ResultPointer & AlignmentMask)
    {
        AlignmentOffset = Alignment - (ResultPointer & AlignmentMask);
    }

    return AlignmentOffset;
}

internal void *PushSize_(memory_arena *Arena, memory_size SizeInit, memory_size Alignment = DEFAULT_ALIGNMENT)
{
    memory_size AlignmentOffset = GetAlignmentOffset(Arena, Alignment);
    memory_size Size = SizeInit + AlignmentOffset;

    Assert((Arena->Used + Size) <= Arena->Size);

    void *Result = Arena->Base + Arena->Used + AlignmentOffset;
    Arena->Used += Size;

    Assert(Size >= SizeInit);

    return Result;
}

#define PushStruct(Arena, type)                         (type *)PushSize_(Arena, sizeof(type))
#define PushArray(Arena, Count, type)                   (type *)PushSize_(Arena, (Count) * sizeof(type))
#define PushSize(Arena, Size)                           PushSize_(Arena, Size)
#define PushStructAligned(Arena, type, Alignment)       (type *)PushSize_(Arena, sizeof(type), Alignment)
#define PushArrayAligned(Arena, Count, type, Alignment) (type *)PushSize_(Arena, (Count) * sizeof(type), Alignment)

internal void ResetArena(memory_arena *Arena)
{
    Arena->Used = 0;
}

internal void SubArena(memory_arena *Result, memory_arena *Parent, memory_size Size, memory_size Alignment = 16)
{
    Result->Size = Size;
    Result->Base = (uint8 *)PushSize_(Parent, Size, Alignment);
    Result->Used = 0;
    Result->TempCount = 0;
}

struct temporary_memory
{
    memory_arena *Arena;
    memory_size Used;
};

internal temporary_memory BeginTemporaryMemory(memory_arena *Arena)
{
    temporary_memory Result;
    Result.Arena = Arena;
    Result.Used = Arena->Used;
    ++Arena->TempCount;

    return Result;
}

internal void EndTemporaryMemory(temporary_memory TempMem)
{
    memory_arena *Arena = TempMem.Arena;
    Assert(Arena->Used >= TempMem.Used);
    Arena->Used = TempMem.Used;
    Assert(Arena->TempCount > 0);
    --Arena->TempCount;
}

internal void CheckArena(memory_arena *Arena)
{
    Assert(Arena->TempCount == 0);
}

internal void ZeroSize(memory_size Size, void *Ptr)
{
    uint8 *Byte = (uint8 *)Ptr;
    while (Size--)
    {
        *Byte++ = 0;
    }
}

#define ZeroStruct(Instance)      ZeroSize(sizeof(Instance), &(Instance))
#define ZeroArray(Count, Pointer) ZeroSize((Count) * sizeof((Pointer)[0]), Pointer)

internal void CopySize(memory_size Size, void *SourceInit, void *DestInit)
{
    uint64 *Source64 = (uint64 *)SourceInit;
    uint64 *Dest64   = (uint64 *)DestInit;
    while (Size >= 8)
    {
        *Dest64++ = *Source64++;
        Size -= 8;
    }

    uint8 *Source = (uint8 *)Source64;
    uint8 *Dest   = (uint8 *)Dest64;
    while (Size--)
    {
        *Dest++ = *Source++;
    }
}

#endif
