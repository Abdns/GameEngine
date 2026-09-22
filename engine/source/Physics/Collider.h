#ifndef COLLIDER_H
#define COLLIDER_H

#include "Types.h"
#include "EngineMath.h"

struct collision
{
    void   *Vertices;
    uint32 *Indices;
    uint32  VertexStride;
    uint32  VertexCount;
    uint32  IndexCount;
    Vector3 BoundsMin;
    Vector3 BoundsMax;
};

inline Vector3 GetCollisionMeshVertex(collision *Mesh, uint32 Index)
{
    return *(Vector3 *)&((uint8 *)Mesh->Vertices)[(uint64)Index * Mesh->VertexStride];
}

inline void ComputeWorldAABB(Vector3 Position, Quaternion Orientation, Vector3 LocalMin, Vector3 LocalMax, Vector3 *OutMin, Vector3 *OutMax)
{
    Vector3 Min = Vector3( REAL32_LARGE,  REAL32_LARGE,  REAL32_LARGE);
    Vector3 Max = Vector3(-REAL32_LARGE, -REAL32_LARGE, -REAL32_LARGE);

    for (uint32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
    {
        Vector3 Corner = Vector3((CornerIndex & 1) ? LocalMax.X : LocalMin.X,
                                 (CornerIndex & 2) ? LocalMax.Y : LocalMin.Y,
                                 (CornerIndex & 4) ? LocalMax.Z : LocalMin.Z);

        Vector3 P = Position + QuatRotate(Orientation, Corner);

        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Min.Elements[Axis] = Minimum(Min.Elements[Axis], P.Elements[Axis]);
            Max.Elements[Axis] = Maximum(Max.Elements[Axis], P.Elements[Axis]);
        }
    }

    *OutMin = Min;
    *OutMax = Max;
}

#endif
