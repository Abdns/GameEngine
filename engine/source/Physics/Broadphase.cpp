#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#include "SimEntity.cpp"

struct body_pair
{
    uint32 A;
    uint32 B;
};

struct broadphase
{
    uint32    *Order;
    uint32     OrderCount;
    uint32     OrderCapacity;

    body_pair *Pairs;
    uint32     PairCount;
    uint32     PairCapacity;
};

internal void BroadphaseInit(broadphase *Broad, memory_arena *Arena, uint32 EntityCapacity, uint32 PairCapacity)
{
    Broad->Order         = PushArray(Arena, EntityCapacity, uint32);
    Broad->OrderCount    = 0;
    Broad->OrderCapacity = EntityCapacity;

    Broad->Pairs        = PushArray(Arena, PairCapacity, body_pair);
    Broad->PairCount    = 0;
    Broad->PairCapacity = PairCapacity;
}

internal void BroadphaseBuildOrder(broadphase *Broad, sim_entity *Entities, uint32 EntityCount)
{
    Broad->OrderCount = 0;

    for (uint32 Index = 0; Index < EntityCount && Broad->OrderCount < Broad->OrderCapacity; ++Index)
    {
        if (Entities[Index].Flags & EntityFlag_Simulates)
        {
            Broad->Order[Broad->OrderCount++] = Index;
        }
    }
}

internal void BroadphaseSortByMinX(broadphase *Broad, sim_entity *Entities)
{
    for (uint32 Index = 1; Index < Broad->OrderCount; ++Index)
    {
        uint32 Entity = Broad->Order[Index];
        real32 Key    = Entities[Entity].Bounds.Min.X;

        uint32 Scan = Index;
        while (Scan > 0 && Entities[Broad->Order[Scan - 1]].Bounds.Min.X > Key)
        {
            Broad->Order[Scan] = Broad->Order[Scan - 1];
            --Scan;
        }

        Broad->Order[Scan] = Entity;
    }
}

internal uint32 BroadphaseFindPairs(broadphase *Broad, sim_entity *Entities)
{
    Broad->PairCount = 0;

    if (Broad->OrderCount < 2)
    {
        return 0;
    }

    BroadphaseSortByMinX(Broad, Entities);

    for (uint32 IndexA = 0; IndexA < Broad->OrderCount; ++IndexA)
    {
        uint32      EntityA = Broad->Order[IndexA];
        sim_entity *A       = Entities + EntityA;

        for (uint32 IndexB = IndexA + 1; IndexB < Broad->OrderCount; ++IndexB)
        {
            uint32      EntityB = Broad->Order[IndexB];
            sim_entity *B       = Entities + EntityB;

            if (B->Bounds.Min.X > A->Bounds.Max.X)
            {
                break;
            }

            bool32 MovableA = A->Updatable && A->InvMass > 0.0f;
            bool32 MovableB = B->Updatable && B->InvMass > 0.0f;
            if (!MovableA && !MovableB)
            {
                continue;
            }

            if (A->Bounds.Max.Y < B->Bounds.Min.Y || B->Bounds.Max.Y < A->Bounds.Min.Y ||
                A->Bounds.Max.Z < B->Bounds.Min.Z || B->Bounds.Max.Z < A->Bounds.Min.Z)
            {
                continue;
            }

            if (Broad->PairCount >= Broad->PairCapacity)
            {
                DebugLog("Broadphase pair capacity %u reached, remaining overlaps are ignored this substep\n", Broad->PairCapacity);
                return Broad->PairCount;
            }

            body_pair *Pair = Broad->Pairs + Broad->PairCount++;
            Pair->A = EntityA;
            Pair->B = EntityB;
        }
    }

    return Broad->PairCount;
}
