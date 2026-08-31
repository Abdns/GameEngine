#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#include "Collider.h"

#define SHAPE_SPHERE_TOLERANCE     0.02f
#define SHAPE_MIN_SPHERE_VERTICES  64
#define SHAPE_PLANE_TOLERANCE      0.002f
#define SHAPE_VERTEX_TOLERANCE     0.0001f
#define SHAPE_MAX_PLANES           1024
#define SHAPE_FACE_TOLERANCE       0.001f

enum shape_type
{
    Shape_Sphere = 0,
    Shape_Convex,
};

struct shape_plane
{
    Vector3 Normal;
    real32  Distance;
};

struct shape_face
{
    uint32 FirstVertex;
    uint32 VertexCount;
};

struct collision_shape
{
    shape_type   Type;

    Vector3      Center;
    real32       Radius;

    Vector3     *Vertices;
    uint32       VertexCount;
    shape_plane *Planes;
    uint32       PlaneCount;

    shape_face  *Faces;
    uint32      *FaceVertices;
    uint32       FaceVertexCount;

    Vector3      BoundsMin;
    Vector3      BoundsMax;
    real32       InertiaFactor;
};

inline Vector3 ShapePerpendicular(Vector3 N)
{
    Vector3 Axis = (Abs(N.X) <= Abs(N.Y) && Abs(N.X) <= Abs(N.Z)) ? Vector3(1.0f, 0.0f, 0.0f)
                 : (Abs(N.Y) <= Abs(N.Z))                         ? Vector3(0.0f, 1.0f, 0.0f)
                                                                  : Vector3(0.0f, 0.0f, 1.0f);

    return Normalize(Cross(N, Axis));
}

internal void ShapeBuildFaces(memory_arena *Arena, collision_shape *Shape)
{
    Shape->Faces           = PushArray(Arena, Shape->PlaneCount, shape_face);
    Shape->FaceVertexCount = 0;

    temporary_memory Scratch = BeginTemporaryMemory(Arena);

    uint32 *Scratchpad = PushArray(Arena, (memory_size)Shape->PlaneCount * Shape->VertexCount, uint32);
    real32 *Angles     = PushArray(Arena, Shape->VertexCount, real32);

    for (uint32 PlaneIndex = 0; PlaneIndex < Shape->PlaneCount; ++PlaneIndex)
    {
        shape_plane *Plane = Shape->Planes + PlaneIndex;
        uint32      *Loop  = Scratchpad + (memory_size)PlaneIndex * Shape->VertexCount;
        uint32       Count = 0;

        Vector3 Centroid = Vector3(0.0f, 0.0f, 0.0f);

        for (uint32 Index = 0; Index < Shape->VertexCount; ++Index)
        {
            if (Abs(Dot(Plane->Normal, Shape->Vertices[Index]) - Plane->Distance) > SHAPE_FACE_TOLERANCE)
            {
                continue;
            }

            Loop[Count++] = Index;
            Centroid += Shape->Vertices[Index];
        }

        if (Count < 3)
        {
            Shape->Faces[PlaneIndex].FirstVertex = 0;
            Shape->Faces[PlaneIndex].VertexCount = 0;
            continue;
        }

        Centroid = (1.0f / (real32)Count) * Centroid;

        Vector3 Tangent   = ShapePerpendicular(Plane->Normal);
        Vector3 Bitangent = Cross(Plane->Normal, Tangent);

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            Vector3 Delta = Shape->Vertices[Loop[Index]] - Centroid;
            Angles[Index] = ATan2(Dot(Delta, Bitangent), Dot(Delta, Tangent));
        }

        for (uint32 Index = 1; Index < Count; ++Index)
        {
            uint32 Vertex = Loop[Index];
            real32 Angle  = Angles[Index];

            uint32 Scan = Index;
            while (Scan > 0 && Angles[Scan - 1] > Angle)
            {
                Loop[Scan]   = Loop[Scan - 1];
                Angles[Scan] = Angles[Scan - 1];
                --Scan;
            }

            Loop[Scan]   = Vertex;
            Angles[Scan] = Angle;
        }

        Shape->Faces[PlaneIndex].FirstVertex = Shape->FaceVertexCount;
        Shape->Faces[PlaneIndex].VertexCount = Count;
        Shape->FaceVertexCount += Count;
    }

    EndTemporaryMemory(Scratch);

    Shape->FaceVertices = PushArray(Arena, Shape->FaceVertexCount, uint32);

    Scratch    = BeginTemporaryMemory(Arena);
    Scratchpad = PushArray(Arena, (memory_size)Shape->PlaneCount * Shape->VertexCount, uint32);
    Angles     = PushArray(Arena, Shape->VertexCount, real32);

    for (uint32 PlaneIndex = 0; PlaneIndex < Shape->PlaneCount; ++PlaneIndex)
    {
        shape_face  *Face  = Shape->Faces + PlaneIndex;
        shape_plane *Plane = Shape->Planes + PlaneIndex;

        if (!Face->VertexCount)
        {
            continue;
        }

        uint32 *Loop  = Scratchpad;
        uint32  Count = 0;

        Vector3 Centroid = Vector3(0.0f, 0.0f, 0.0f);

        for (uint32 Index = 0; Index < Shape->VertexCount; ++Index)
        {
            if (Abs(Dot(Plane->Normal, Shape->Vertices[Index]) - Plane->Distance) > SHAPE_FACE_TOLERANCE)
            {
                continue;
            }

            Loop[Count++] = Index;
            Centroid += Shape->Vertices[Index];
        }

        Centroid = (1.0f / (real32)Count) * Centroid;

        Vector3 Tangent   = ShapePerpendicular(Plane->Normal);
        Vector3 Bitangent = Cross(Plane->Normal, Tangent);

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            Vector3 Delta = Shape->Vertices[Loop[Index]] - Centroid;
            Angles[Index] = ATan2(Dot(Delta, Bitangent), Dot(Delta, Tangent));
        }

        for (uint32 Index = 1; Index < Count; ++Index)
        {
            uint32 Vertex = Loop[Index];
            real32 Angle  = Angles[Index];

            uint32 Scan = Index;
            while (Scan > 0 && Angles[Scan - 1] > Angle)
            {
                Loop[Scan]   = Loop[Scan - 1];
                Angles[Scan] = Angles[Scan - 1];
                --Scan;
            }

            Loop[Scan]   = Vertex;
            Angles[Scan] = Angle;
        }

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            Shape->FaceVertices[Face->FirstVertex + Index] = Loop[Index];
        }
    }

    EndTemporaryMemory(Scratch);
}

internal bool32 ShapeDetectSphere(collision *Mesh, Vector3 Center, real32 *OutRadius)
{
    if (Mesh->VertexCount < SHAPE_MIN_SPHERE_VERTICES)
    {
        return false;
    }

    real32 MinRadius =  REAL32_LARGE;
    real32 MaxRadius = 0.0f;

    for (uint32 Index = 0; Index < Mesh->VertexCount; ++Index)
    {
        real32 Radius = Length(GetCollisionMeshVertex(Mesh, Index) - Center);

        MinRadius = Minimum(MinRadius, Radius);
        MaxRadius = Maximum(MaxRadius, Radius);
    }

    if (MaxRadius <= Epsilon32 || (MaxRadius - MinRadius) > SHAPE_SPHERE_TOLERANCE * MaxRadius)
    {
        return false;
    }

    Vector3 Extent      = Mesh->BoundsMax - Mesh->BoundsMin;
    real32  MinDiameter = 2.0f * MaxRadius * (1.0f - SHAPE_SPHERE_TOLERANCE);

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (Extent.Elements[Axis] < MinDiameter)
        {
            return false;
        }
    }

    *OutRadius = 0.5f * (MinRadius + MaxRadius);

    return true;
}

internal uint32 ShapeCollectVertices(collision *Mesh, Vector3 *Vertices, uint32 MaxVertices)
{
    uint32 Count = 0;

    for (uint32 Index = 0; Index < Mesh->VertexCount && Count < MaxVertices; ++Index)
    {
        Vector3 P = GetCollisionMeshVertex(Mesh, Index);

        bool32 Duplicate = false;
        for (uint32 Existing = 0; Existing < Count; ++Existing)
        {
            if (LengthSq(Vertices[Existing] - P) < SHAPE_VERTEX_TOLERANCE * SHAPE_VERTEX_TOLERANCE)
            {
                Duplicate = true;
                break;
            }
        }

        if (!Duplicate)
        {
            Vertices[Count++] = P;
        }
    }

    return Count;
}

internal uint32 ShapeCollectPlanes(collision *Mesh, Vector3 Center, shape_plane *Planes, uint32 MaxPlanes)
{
    uint32 Count = 0;

    for (uint32 Index = 0; Index + 3 <= Mesh->IndexCount; Index += 3)
    {
        Vector3 A = GetCollisionMeshVertex(Mesh, Mesh->Indices[Index + 0]);
        Vector3 B = GetCollisionMeshVertex(Mesh, Mesh->Indices[Index + 1]);
        Vector3 C = GetCollisionMeshVertex(Mesh, Mesh->Indices[Index + 2]);

        Vector3 Normal = Cross(B - A, C - A);
        real32  Len    = Length(Normal);
        if (Len < Epsilon32)
        {
            continue;
        }

        shape_plane Plane;
        Plane.Normal   = (1.0f / Len) * Normal;
        Plane.Distance = Dot(Plane.Normal, A);

        if (Dot(Plane.Normal, Center) - Plane.Distance > 0.0f)
        {
            Plane.Normal   = -1.0f * Plane.Normal;
            Plane.Distance = -Plane.Distance;
        }

        bool32 Duplicate = false;
        for (uint32 Existing = 0; Existing < Count; ++Existing)
        {
            if (Dot(Planes[Existing].Normal, Plane.Normal) > 1.0f - SHAPE_PLANE_TOLERANCE &&
                Abs(Planes[Existing].Distance - Plane.Distance) < SHAPE_PLANE_TOLERANCE)
            {
                Duplicate = true;
                break;
            }
        }

        if (Duplicate)
        {
            continue;
        }

        if (Count >= MaxPlanes)
        {
            DebugLog("Shape has more than %u distinct planes, collision will be approximate\n", MaxPlanes);
            break;
        }

        Planes[Count++] = Plane;
    }

    return Count;
}

internal collision_shape BuildCollisionShape(memory_arena *Arena, collision Mesh)
{
    collision_shape Shape = {};

    Shape.BoundsMin = Mesh.BoundsMin;
    Shape.BoundsMax = Mesh.BoundsMax;
    Shape.Center    = 0.5f * (Mesh.BoundsMin + Mesh.BoundsMax);

    Vector3 Extent = Mesh.BoundsMax - Mesh.BoundsMin;

    if (ShapeDetectSphere(&Mesh, Shape.Center, &Shape.Radius))
    {
        Shape.Type          = Shape_Sphere;
        Shape.InertiaFactor = 0.4f * Shape.Radius * Shape.Radius;

        DebugLog("Shape: sphere, radius %.3f, replaced %u vertices and %u triangles\n", Shape.Radius, Mesh.VertexCount, Mesh.IndexCount / 3);

        return Shape;
    }

    Shape.Type          = Shape_Convex;
    Shape.Radius        = 0.5f * Length(Extent);
    Shape.InertiaFactor = (Extent.X * Extent.X + Extent.Y * Extent.Y + Extent.Z * Extent.Z) / 18.0f;

    temporary_memory Scratch = BeginTemporaryMemory(Arena);

    Vector3     *ScratchVertices = PushArray(Arena, Mesh.VertexCount, Vector3);
    shape_plane *ScratchPlanes   = PushArray(Arena, SHAPE_MAX_PLANES, shape_plane);

    uint32 VertexCount = ShapeCollectVertices(&Mesh, ScratchVertices, Mesh.VertexCount);
    uint32 PlaneCount  = ShapeCollectPlanes(&Mesh, Shape.Center, ScratchPlanes, SHAPE_MAX_PLANES);

    EndTemporaryMemory(Scratch);

    Shape.Vertices    = PushArray(Arena, VertexCount, Vector3);
    Shape.VertexCount = VertexCount;
    Shape.Planes      = PushArray(Arena, PlaneCount, shape_plane);
    Shape.PlaneCount  = PlaneCount;

    ShapeCollectVertices(&Mesh, Shape.Vertices, VertexCount);
    ShapeCollectPlanes(&Mesh, Shape.Center, Shape.Planes, PlaneCount);

    ShapeBuildFaces(Arena, &Shape);

    DebugLog("Shape: convex, %u/%u vertices, %u/%u planes\n", VertexCount, Mesh.VertexCount, PlaneCount, Mesh.IndexCount / 3);

    return Shape;
}

internal bool32 ShapeDeepestPlane(collision_shape *Shape, Vector3 P, uint32 *OutPlane, real32 *OutSignedDistance)
{
    if (!Shape->PlaneCount)
    {
        return false;
    }

    real32 BestDistance = -REAL32_LARGE;
    uint32 BestPlane    = 0;

    for (uint32 Index = 0; Index < Shape->PlaneCount; ++Index)
    {
        real32 SignedDistance = Dot(Shape->Planes[Index].Normal, P) - Shape->Planes[Index].Distance;

        if (SignedDistance > BestDistance)
        {
            BestDistance = SignedDistance;
            BestPlane    = Index;
        }
    }

    *OutPlane          = BestPlane;
    *OutSignedDistance = BestDistance;

    return true;
}
