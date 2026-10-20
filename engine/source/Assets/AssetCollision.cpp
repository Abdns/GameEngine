#pragma once

// Included by AssetStore.cpp after its storage definitions.
#include "Collider.h"

// Collision views over stored mesh data, or an arena-backed bounding box.

internal collision AssetCollisionMesh(asset_store *Store, uint32 MeshHandle)
{
    collision Mesh = {};

    Mesh.Vertices     = AssetMeshVertices(Store, MeshHandle);
    Mesh.VertexStride = sizeof(enga_vertex);
    Mesh.VertexCount  = Store->MeshVertexCount[MeshHandle];
    Mesh.Indices      = AssetMeshIndices(Store, MeshHandle);
    Mesh.IndexCount   = Store->MeshIndexCount[MeshHandle];
    Mesh.BoundsMin    = Store->MeshBoundsMin[MeshHandle];
    Mesh.BoundsMax    = Store->MeshBoundsMax[MeshHandle];

    return Mesh;
}

internal collision AssetCollisionBox(asset_store *Store, memory_arena *Arena, uint32 MeshHandle)
{
    local_persist uint32 BoxIndices[36] =
    {
        0, 2, 6,  0, 6, 4,
        1, 5, 7,  1, 7, 3,
        0, 4, 5,  0, 5, 1,
        2, 3, 7,  2, 7, 6,
        0, 1, 3,  0, 3, 2,
        4, 6, 7,  4, 7, 5,
    };

    Vector3 Min = Store->MeshBoundsMin[MeshHandle];
    Vector3 Max = Store->MeshBoundsMax[MeshHandle];

    Vector3 *Corners = PushArray(Arena, 8, Vector3);
    for (uint32 Corner = 0; Corner < 8; ++Corner)
    {
        Corners[Corner] = Vector3((Corner & 1) ? Max.X : Min.X,
                                  (Corner & 2) ? Max.Y : Min.Y,
                                  (Corner & 4) ? Max.Z : Min.Z);
    }

    collision Box = {};
    Box.Vertices     = Corners;
    Box.VertexStride = sizeof(Vector3);
    Box.VertexCount  = 8;
    Box.Indices      = BoxIndices;
    Box.IndexCount   = ArrayCount(BoxIndices);
    Box.BoundsMin    = Min;
    Box.BoundsMax    = Max;

    return Box;
}
