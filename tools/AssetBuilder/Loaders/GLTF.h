#ifndef GLTF_H
#define GLTF_H

#include <math.h>

#include "Types.h"
#include "Memory.h"
#include "EngaFormat.h"
#include "Json.h"

#define GLTF_FLOAT  5126
#define GLTF_USHORT 5123
#define GLTF_UINT   5125

struct gltf_file
{
    json_value *Root;
    json_value *Accessors;
    json_value *BufferViews;
    uint8      *Bin;
    uint32      BinSize;
};

internal gltf_file ParseGLTF(memory_arena *Arena, void *Data, uint32 Size)
{
    gltf_file Result = {};

    Assert(Data && Size);

    uint32 ErrorOffset = 0;
    Result.Root = JsonParse(Arena, Data, Size, &ErrorOffset);
    Assert(Result.Root);

    Result.Accessors   = JsonGet(Result.Root, "accessors");
    Result.BufferViews = JsonGet(Result.Root, "bufferViews");

    return Result;
}

internal uint32 GLTFComponentSize(uint32 ComponentType)
{
    switch (ComponentType)
    {
        case GLTF_FLOAT:  return 4;
        case GLTF_UINT:   return 4;
        case GLTF_USHORT: return 2;
    }

    return 0;
}

internal uint8 *GLTFViewData(gltf_file *File, uint32 ViewIndex, uint32 *OutSize, uint32 *OutStride)
{
    json_value *View = JsonAt(File->BufferViews, ViewIndex);
    Assert(View);

    if (JsonU32(JsonGet(View, "buffer"), 0) != 0)
    {
        DebugLog("GLTF: bufferView %u refers to a second buffer, unsupported\n", ViewIndex);
        return 0;
    }

    uint32 Offset = JsonU32(JsonGet(View, "byteOffset"), 0);
    uint32 Length = JsonU32(JsonGet(View, "byteLength"), 0);
    Assert(Offset <= File->BinSize && Length <= File->BinSize - Offset);

    *OutSize   = Length;
    *OutStride = JsonU32(JsonGet(View, "byteStride"), 0);
    return File->Bin + Offset;
}

internal uint8 *GLTFAccessorData(gltf_file *File, json_value *AccessorIndex, uint32 ComponentCount, uint32 *OutCount, uint32 *OutComponentType, uint32 *OutStride)
{
    *OutCount         = 0;
    *OutComponentType = 0;
    *OutStride        = 0;

    if (!AccessorIndex)
    {
        return 0;
    }

    json_value *Accessor = JsonAt(File->Accessors, JsonU32(AccessorIndex, 0));
    Assert(Accessor);

    json_value *ViewIndex = JsonGet(Accessor, "bufferView");
    if (!ViewIndex)
    {
        DebugLog("GLTF: accessor without a bufferView is unsupported\n");
        return 0;
    }

    uint32 ComponentType = JsonU32(JsonGet(Accessor, "componentType"), 0);
    uint32 ElementSize   = GLTFComponentSize(ComponentType) * ComponentCount;
    if (!ElementSize)
    {
        DebugLog("GLTF: unsupported componentType %u\n", ComponentType);
        return 0;
    }

    uint32 ViewSize   = 0;
    uint32 ViewStride = 0;
    uint8 *ViewData   = GLTFViewData(File, JsonU32(ViewIndex, 0), &ViewSize, &ViewStride);
    if (!ViewData)
    {
        return 0;
    }

    uint32 Stride = ViewStride ? ViewStride : ElementSize;
    Assert(Stride >= ElementSize);

    uint32 Offset = JsonU32(JsonGet(Accessor, "byteOffset"), 0);
    uint32 Count  = JsonU32(JsonGet(Accessor, "count"), 0);
    Assert(Count && Offset <= ViewSize);

    uint64 Span = (uint64)(Count - 1) * Stride + ElementSize;
    Assert(Span <= (uint64)(ViewSize - Offset));

    *OutCount         = Count;
    *OutComponentType = ComponentType;
    *OutStride        = Stride;
    return ViewData + Offset;
}

struct gltf_geometry
{
    void       *Blob;
    uint64      BlobSize;
    enga_vertex *Vertices;
    uint32     *Indices;
    uint32      VertexCount;
    uint32      IndexCount;
};

internal gltf_geometry GLTFMeshGeometry(memory_arena *Arena, gltf_file *File, json_value *Mesh)
{
    gltf_geometry Result = {};

    json_value *Prim       = JsonAt(JsonGet(Mesh, "primitives"), 0);
    json_value *Attributes = JsonGet(Prim, "attributes");

    uint32 PosCount  = 0;
    uint32 PosType   = 0;
    uint32 PosStride = 0;
    uint8 *Pos = GLTFAccessorData(File, JsonGet(Attributes, "POSITION"), 3, &PosCount, &PosType, &PosStride);
    Assert(Pos && PosType == GLTF_FLOAT && PosCount);

    uint32 NormalCount  = 0;
    uint32 NormalType   = 0;
    uint32 NormalStride = 0;
    uint8 *Normal = GLTFAccessorData(File, JsonGet(Attributes, "NORMAL"), 3, &NormalCount, &NormalType, &NormalStride);
    if (Normal && NormalType != GLTF_FLOAT)
    {
        DebugLog("GLTF: NORMAL is not float, ignored\n");
        Normal = 0;
    }

    uint32 UVCount  = 0;
    uint32 UVType   = 0;
    uint32 UVStride = 0;
    uint8 *UV = GLTFAccessorData(File, JsonGet(Attributes, "TEXCOORD_0"), 2, &UVCount, &UVType, &UVStride);
    if (UV && UVType != GLTF_FLOAT)
    {
        DebugLog("GLTF: TEXCOORD_0 is not float, ignored\n");
        UV = 0;
    }

    json_value *IndicesRef = JsonGet(Prim, "indices");

    uint32 SourceIndexCount = 0;
    uint32 IndexType        = 0;
    uint32 IndexStride      = 0;
    uint8 *SourceIndices = GLTFAccessorData(File, IndicesRef, 1, &SourceIndexCount, &IndexType, &IndexStride);
    if (IndicesRef && !SourceIndices)
    {
        DebugLog("GLTF: primitive declares indices but they could not be read\n");
        return Result;
    }

    Assert(!SourceIndices || IndexType == GLTF_USHORT || IndexType == GLTF_UINT);

    uint32 VertexCount = PosCount;
    uint32 IndexCount  = SourceIndices ? SourceIndexCount : PosCount;

    uint64 VertexBytes = (uint64)VertexCount * sizeof(enga_vertex);
    uint64 IndexBytes  = (uint64)IndexCount * sizeof(uint32);

    uint8 *Blob = (uint8 *)PushSize(Arena, VertexBytes + IndexBytes);

    enga_vertex *Out     = (enga_vertex *)Blob;
    uint32     *OutIndx = (uint32 *)(Blob + VertexBytes);

    for (uint32 v = 0; v < VertexCount; ++v)
    {
        real32 *SrcPos = (real32 *)(Pos + (memory_size)v * PosStride);

        enga_vertex *Dst = Out + v;
        Dst->Pos[0]    = SrcPos[0];
        Dst->Pos[1]    = SrcPos[1];
        Dst->Pos[2]    = SrcPos[2];
        Dst->Normal[0] = 0.0f;
        Dst->Normal[1] = 1.0f;
        Dst->Normal[2] = 0.0f;
        Dst->Color[0]  = 1.0f;
        Dst->Color[1]  = 1.0f;
        Dst->Color[2]  = 1.0f;
        Dst->UV[0]     = 0.0f;
        Dst->UV[1]     = 0.0f;

        if (Normal && v < NormalCount)
        {
            real32 *SrcNormal = (real32 *)(Normal + (memory_size)v * NormalStride);
            Dst->Normal[0] = SrcNormal[0];
            Dst->Normal[1] = SrcNormal[1];
            Dst->Normal[2] = SrcNormal[2];
        }

        if (UV && v < UVCount)
        {
            real32 *SrcUV = (real32 *)(UV + (memory_size)v * UVStride);
            Dst->UV[0] = SrcUV[0];
            Dst->UV[1] = SrcUV[1];
        }
    }

    for (uint32 i = 0; i < IndexCount; ++i)
    {
        uint32 Src = i;
        if (SourceIndices)
        {
            uint8 *SrcIndex = SourceIndices + (memory_size)i * IndexStride;
            Src = (IndexType == GLTF_USHORT) ? *(uint16 *)SrcIndex : *(uint32 *)SrcIndex;
        }
        Assert(Src < VertexCount);

        OutIndx[i] = Src;
    }

    if (!Normal)
    {
        DebugLog("GLTF: mesh has no NORMAL attribute, generating from geometry\n");

        for (uint32 v = 0; v < VertexCount; ++v)
        {
            Out[v].Normal[0] = 0.0f;
            Out[v].Normal[1] = 0.0f;
            Out[v].Normal[2] = 0.0f;
        }

        for (uint32 i = 0; i + 2 < IndexCount; i += 3)
        {
            enga_vertex *A = Out + OutIndx[i + 0];
            enga_vertex *B = Out + OutIndx[i + 1];
            enga_vertex *C = Out + OutIndx[i + 2];

            real32 ABx = B->Pos[0] - A->Pos[0];
            real32 ABy = B->Pos[1] - A->Pos[1];
            real32 ABz = B->Pos[2] - A->Pos[2];

            real32 ACx = C->Pos[0] - A->Pos[0];
            real32 ACy = C->Pos[1] - A->Pos[1];
            real32 ACz = C->Pos[2] - A->Pos[2];

            real32 Nx = ABy * ACz - ABz * ACy;
            real32 Ny = ABz * ACx - ABx * ACz;
            real32 Nz = ABx * ACy - ABy * ACx;

            enga_vertex *Face[3] = { A, B, C };
            for (uint32 f = 0; f < 3; ++f)
            {
                Face[f]->Normal[0] += Nx;
                Face[f]->Normal[1] += Ny;
                Face[f]->Normal[2] += Nz;
            }
        }

        for (uint32 v = 0; v < VertexCount; ++v)
        {
            real32 *N = Out[v].Normal;

            real32 Length = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
            if (Length > 1e-8f)
            {
                N[0] /= Length;
                N[1] /= Length;
                N[2] /= Length;
            }
            else
            {
                N[0] = 0.0f;
                N[1] = 1.0f;
                N[2] = 0.0f;
            }
        }

        real32 Center[3] = { 0.0f, 0.0f, 0.0f };
        for (uint32 v = 0; v < VertexCount; ++v)
        {
            Center[0] += Out[v].Pos[0];
            Center[1] += Out[v].Pos[1];
            Center[2] += Out[v].Pos[2];
        }
        Center[0] /= (real32)VertexCount;
        Center[1] /= (real32)VertexCount;
        Center[2] /= (real32)VertexCount;

        for (uint32 v = 0; v < VertexCount; ++v)
        {
            real32 *N = Out[v].Normal;

            real32 Outward[3];
            Outward[0] = Out[v].Pos[0] - Center[0];
            Outward[1] = Out[v].Pos[1] - Center[1];
            Outward[2] = Out[v].Pos[2] - Center[2];

            real32 Facing = N[0] * Outward[0] + N[1] * Outward[1] + N[2] * Outward[2];
            if (Facing < -1e-4f)
            {
                N[0] = -N[0];
                N[1] = -N[1];
                N[2] = -N[2];
            }
        }
    }

    Result.Blob        = Blob;
    Result.BlobSize    = VertexBytes + IndexBytes;
    Result.Vertices    = Out;
    Result.Indices     = OutIndx;
    Result.VertexCount = VertexCount;
    Result.IndexCount  = IndexCount;

    return Result;
}

#endif
