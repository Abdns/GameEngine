#pragma once

#include "Types.h"
#include "Memory.h"
#include "Strings.h"
#include "EngineMath.h"
#include "EngaFormat.h"

#define ASSET_HANDLE_NONE 0xFFFFFFFF

struct asset_store_budget
{
    uint32 MeshCount;
    uint32 TextureCount;
    uint32 CubemapCount;
    uint32 VertexCount;
    uint32 IndexCount;
    uint32 SkinVertexCount;
    uint64 PixelByteCount;
    uint32 SkeletonCount;
    uint32 JointCount;
    uint32 AnimationCount;
    uint32 ChannelCount;
    uint32 AnimationFloatCount;
};

struct asset_store
{
    enga_vertex      *Vertices;
    uint32           *Indices;
    enga_skin_vertex *SkinVertices;
    uint8            *Pixels;

    uint32 VertexUsed, VertexCapacity;
    uint32 IndexUsed,  IndexCapacity;
    uint32 SkinVertexUsed, SkinVertexCapacity;
    uint64 PixelByteUsed, PixelByteCapacity;

    char    *MeshNames;
    uint32  *MeshFirstVertex;
    uint32  *MeshVertexCount;
    uint32  *MeshFirstIndex;
    uint32  *MeshIndexCount;
    uint32  *MeshFirstSkinVertex;
    Vector3 *MeshBoundsMin;
    Vector3 *MeshBoundsMax;
    uint32   MeshCount, MeshCapacity;

    char      *JointNames;
    uint32    *JointParent;
    Matrix4   *JointInverseBind;
    transform *JointBindPose;
    uint32     JointUsed, JointCapacity;

    char   *SkeletonNames;
    uint32 *SkeletonFirstJoint;
    uint32 *SkeletonJointCount;
    uint32  SkeletonCount, SkeletonCapacity;

    enga_animation_channel *Channels;
    real32                 *AnimationFloats;
    uint32                  ChannelUsed, ChannelCapacity;
    uint32                  AnimationFloatUsed, AnimationFloatCapacity;

    char   *AnimationNames;
    uint32 *AnimationSkeleton;
    real32 *AnimationDuration;
    uint32 *AnimationFirstChannel;
    uint32 *AnimationChannelCount;
    uint32  AnimationCount, AnimationCapacity;

    char   *TextureNames;
    uint64 *TextureFirstByte;
    uint32 *TextureWidth;
    uint32 *TextureHeight;
    uint32 *TextureSRGB;
    uint32 *TextureFormat;
    uint32  TextureCount, TextureCapacity;

    char   *CubemapNames;
    uint64 *CubemapFirstByte;
    uint32 *CubemapFaceSize;
    uint32 *CubemapFormat;
    uint32  CubemapCount, CubemapCapacity;
};

// Memory budget and arena allocation.

internal asset_store_budget AssetStoreBudgetAdd(asset_store_budget A, asset_store_budget B)
{
    asset_store_budget Result;
    Result.MeshCount      = A.MeshCount      + B.MeshCount;
    Result.TextureCount   = A.TextureCount   + B.TextureCount;
    Result.CubemapCount   = A.CubemapCount   + B.CubemapCount;
    Result.VertexCount    = A.VertexCount    + B.VertexCount;
    Result.IndexCount     = A.IndexCount     + B.IndexCount;
    Result.PixelByteCount = A.PixelByteCount + B.PixelByteCount;

    Result.SkinVertexCount     = A.SkinVertexCount     + B.SkinVertexCount;
    Result.SkeletonCount       = A.SkeletonCount       + B.SkeletonCount;
    Result.JointCount          = A.JointCount          + B.JointCount;
    Result.AnimationCount      = A.AnimationCount      + B.AnimationCount;
    Result.ChannelCount        = A.ChannelCount        + B.ChannelCount;
    Result.AnimationFloatCount = A.AnimationFloatCount + B.AnimationFloatCount;

    return Result;
}

internal void AssetStoreInit(asset_store *Store, memory_arena *Arena, asset_store_budget Budget)
{
    ZeroStruct(*Store);

    Store->VertexCapacity    = Budget.VertexCount;
    Store->IndexCapacity     = Budget.IndexCount;
    Store->PixelByteCapacity = Budget.PixelByteCount;
    Store->MeshCapacity      = Budget.MeshCount;
    Store->TextureCapacity   = Budget.TextureCount;
    Store->CubemapCapacity   = Budget.CubemapCount;

    Store->SkinVertexCapacity     = Budget.SkinVertexCount;
    Store->JointCapacity          = Budget.JointCount;
    Store->SkeletonCapacity       = Budget.SkeletonCount;
    Store->ChannelCapacity        = Budget.ChannelCount;
    Store->AnimationFloatCapacity = Budget.AnimationFloatCount;
    Store->AnimationCapacity      = Budget.AnimationCount;

    if (Store->VertexCapacity)
    {
        Store->Vertices = PushArray(Arena, Store->VertexCapacity, enga_vertex);
    }

    if (Store->SkinVertexCapacity)
    {
        Store->SkinVertices = PushArray(Arena, Store->SkinVertexCapacity, enga_skin_vertex);
    }

    if (Store->JointCapacity)
    {
        Store->JointNames       = PushArray(Arena, (memory_size)Store->JointCapacity * ENGA_MAX_ASSET_NAME, char);
        Store->JointParent      = PushArray(Arena, Store->JointCapacity, uint32);
        Store->JointInverseBind = PushArray(Arena, Store->JointCapacity, Matrix4);
        Store->JointBindPose    = PushArray(Arena, Store->JointCapacity, transform);
    }

    if (Store->SkeletonCapacity)
    {
        Store->SkeletonNames      = PushArray(Arena, (memory_size)Store->SkeletonCapacity * ENGA_MAX_ASSET_NAME, char);
        Store->SkeletonFirstJoint = PushArray(Arena, Store->SkeletonCapacity, uint32);
        Store->SkeletonJointCount = PushArray(Arena, Store->SkeletonCapacity, uint32);
    }

    if (Store->ChannelCapacity)
    {
        Store->Channels = PushArray(Arena, Store->ChannelCapacity, enga_animation_channel);
    }

    if (Store->AnimationFloatCapacity)
    {
        Store->AnimationFloats = PushArray(Arena, Store->AnimationFloatCapacity, real32);
    }

    if (Store->AnimationCapacity)
    {
        Store->AnimationNames        = PushArray(Arena, (memory_size)Store->AnimationCapacity * ENGA_MAX_ASSET_NAME, char);
        Store->AnimationSkeleton     = PushArray(Arena, Store->AnimationCapacity, uint32);
        Store->AnimationDuration     = PushArray(Arena, Store->AnimationCapacity, real32);
        Store->AnimationFirstChannel = PushArray(Arena, Store->AnimationCapacity, uint32);
        Store->AnimationChannelCount = PushArray(Arena, Store->AnimationCapacity, uint32);
    }

    if (Store->IndexCapacity)
    {
        Store->Indices = PushArray(Arena, Store->IndexCapacity, uint32);
    }

    if (Store->PixelByteCapacity)
    {
        Store->Pixels = (uint8 *)PushSize(Arena, Store->PixelByteCapacity);
    }

    if (Store->MeshCapacity)
    {
        Store->MeshNames       = PushArray(Arena, (memory_size)Store->MeshCapacity * ENGA_MAX_ASSET_NAME, char);
        Store->MeshFirstVertex = PushArray(Arena, Store->MeshCapacity, uint32);
        Store->MeshVertexCount = PushArray(Arena, Store->MeshCapacity, uint32);
        Store->MeshFirstIndex  = PushArray(Arena, Store->MeshCapacity, uint32);
        Store->MeshIndexCount  = PushArray(Arena, Store->MeshCapacity, uint32);
        Store->MeshFirstSkinVertex = PushArray(Arena, Store->MeshCapacity, uint32);
        Store->MeshBoundsMin   = PushArray(Arena, Store->MeshCapacity, Vector3);
        Store->MeshBoundsMax   = PushArray(Arena, Store->MeshCapacity, Vector3);
    }

    if (Store->TextureCapacity)
    {
        Store->TextureNames     = PushArray(Arena, (memory_size)Store->TextureCapacity * ENGA_MAX_ASSET_NAME, char);
        Store->TextureFirstByte = PushArray(Arena, Store->TextureCapacity, uint64);
        Store->TextureWidth     = PushArray(Arena, Store->TextureCapacity, uint32);
        Store->TextureHeight    = PushArray(Arena, Store->TextureCapacity, uint32);
        Store->TextureSRGB      = PushArray(Arena, Store->TextureCapacity, uint32);
        Store->TextureFormat    = PushArray(Arena, Store->TextureCapacity, uint32);
    }

    if (Store->CubemapCapacity)
    {
        Store->CubemapNames     = PushArray(Arena, (memory_size)Store->CubemapCapacity * ENGA_MAX_ASSET_NAME, char);
        Store->CubemapFirstByte = PushArray(Arena, Store->CubemapCapacity, uint64);
        Store->CubemapFaceSize  = PushArray(Arena, Store->CubemapCapacity, uint32);
        Store->CubemapFormat    = PushArray(Arena, Store->CubemapCapacity, uint32);
    }
}

// Mesh data.

inline Vector3 EngaVertexPosition(enga_vertex *Vertex)
{
    return Vector3(Vertex->Pos[0], Vertex->Pos[1], Vertex->Pos[2]);
}

internal enga_vertex *AssetMeshVertices(asset_store *Store, uint32 Handle)
{
    return &Store->Vertices[Store->MeshFirstVertex[Handle]];
}

internal uint32 *AssetMeshIndices(asset_store *Store, uint32 Handle)
{
    return &Store->Indices[Store->MeshFirstIndex[Handle]];
}

internal enga_skin_vertex *AssetMeshSkinVertices(asset_store *Store, uint32 Handle)
{
    uint32 First = Store->MeshFirstSkinVertex[Handle];
    return (First != ASSET_HANDLE_NONE) ? &Store->SkinVertices[First] : 0;
}

internal uint32 AssetAddMesh(asset_store *Store, const char *Name, enga_vertex *Vertices, uint32 VertexCount, uint32 *Indices, uint32 IndexCount, enga_skin_vertex *Skin)
{
    Assert(Store->MeshCount < Store->MeshCapacity);
    Assert(VertexCount && IndexCount);
    Assert(VertexCount <= Store->VertexCapacity - Store->VertexUsed);
    Assert(IndexCount <= Store->IndexCapacity - Store->IndexUsed);

    uint32 Handle = Store->MeshCount;

    Store->MeshFirstVertex[Handle] = Store->VertexUsed;
    Store->MeshVertexCount[Handle] = VertexCount;
    Store->MeshFirstIndex[Handle]  = Store->IndexUsed;
    Store->MeshIndexCount[Handle]  = IndexCount;

    CopySize((memory_size)VertexCount * sizeof(enga_vertex), Vertices, AssetMeshVertices(Store, Handle));
    CopySize((memory_size)IndexCount * sizeof(uint32), Indices, AssetMeshIndices(Store, Handle));

    Store->MeshFirstSkinVertex[Handle] = ASSET_HANDLE_NONE;
    if (Skin)
    {
        Assert(VertexCount <= Store->SkinVertexCapacity - Store->SkinVertexUsed);

        Store->MeshFirstSkinVertex[Handle] = Store->SkinVertexUsed;
        CopySize((memory_size)VertexCount * sizeof(enga_skin_vertex), Skin, AssetMeshSkinVertices(Store, Handle));
        Store->SkinVertexUsed += VertexCount;
    }

    Vector3 BoundsMin = Vector3( REAL32_LARGE,  REAL32_LARGE,  REAL32_LARGE);
    Vector3 BoundsMax = Vector3(-REAL32_LARGE, -REAL32_LARGE, -REAL32_LARGE);
    for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        Vector3 P = EngaVertexPosition(&Vertices[VertexIndex]);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            BoundsMin.Elements[Axis] = Minimum(BoundsMin.Elements[Axis], P.Elements[Axis]);
            BoundsMax.Elements[Axis] = Maximum(BoundsMax.Elements[Axis], P.Elements[Axis]);
        }
    }
    Store->MeshBoundsMin[Handle] = BoundsMin;
    Store->MeshBoundsMax[Handle] = BoundsMax;

    AppendString(&Store->MeshNames[(memory_size)Handle * ENGA_MAX_ASSET_NAME], ENGA_MAX_ASSET_NAME, 0, Name);

    Store->VertexUsed += VertexCount;
    Store->IndexUsed  += IndexCount;
    Store->MeshCount++;

    return Handle;
}

// Image data.

internal uint8 *AssetTexturePixels(asset_store *Store, uint32 Handle)
{
    return Store->Pixels + Store->TextureFirstByte[Handle];
}

internal uint8 *AssetCubemapPixels(asset_store *Store, uint32 Handle)
{
    return Store->Pixels + Store->CubemapFirstByte[Handle];
}

internal uint32 AssetAddTexture(asset_store *Store, const char *Name, void *Pixels, uint32 Width, uint32 Height, bool32 SRGB, asset_image_format Format)
{
    Assert(Store->TextureCount < Store->TextureCapacity);

    uint64 ByteSize = (uint64)Width * Height * AssetImageFormatBytes(Format);
    Assert(Store->PixelByteUsed + ByteSize <= Store->PixelByteCapacity);

    uint32 Handle = Store->TextureCount;

    Store->TextureFirstByte[Handle] = Store->PixelByteUsed;
    CopySize(ByteSize, Pixels, AssetTexturePixels(Store, Handle));

    AppendString(&Store->TextureNames[(memory_size)Handle * ENGA_MAX_ASSET_NAME], ENGA_MAX_ASSET_NAME, 0, Name);
    Store->TextureWidth[Handle]  = Width;
    Store->TextureHeight[Handle] = Height;
    Store->TextureSRGB[Handle]   = SRGB;
    Store->TextureFormat[Handle] = (uint32)Format;
    Store->TextureCount++;
    Store->PixelByteUsed += ByteSize;

    return Handle;
}

internal uint32 AssetAddCubemap(asset_store *Store, const char *Name, void *Pixels, uint32 FaceSize, asset_image_format Format)
{
    Assert(Store->CubemapCount < Store->CubemapCapacity);

    uint64 ByteSize = (uint64)FaceSize * FaceSize * 6 * AssetImageFormatBytes(Format);
    Assert(Store->PixelByteUsed + ByteSize <= Store->PixelByteCapacity);

    uint32 Handle = Store->CubemapCount;

    Store->CubemapFirstByte[Handle] = Store->PixelByteUsed;
    CopySize(ByteSize, Pixels, AssetCubemapPixels(Store, Handle));

    AppendString(&Store->CubemapNames[(memory_size)Handle * ENGA_MAX_ASSET_NAME], ENGA_MAX_ASSET_NAME, 0, Name);
    Store->CubemapFaceSize[Handle] = FaceSize;
    Store->CubemapFormat[Handle]   = (uint32)Format;
    Store->CubemapCount++;
    Store->PixelByteUsed += ByteSize;

    return Handle;
}

// Skeleton and animation data.

internal uint32 AssetAddSkeleton(asset_store *Store, const char *Name, enga_joint *Joints, uint32 JointCount)
{
    Assert(Store->SkeletonCount < Store->SkeletonCapacity);
    Assert(JointCount && JointCount <= Store->JointCapacity - Store->JointUsed);

    uint32 Handle = Store->SkeletonCount;
    uint32 First  = Store->JointUsed;

    Store->SkeletonFirstJoint[Handle] = First;
    Store->SkeletonJointCount[Handle] = JointCount;

    for (uint32 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
    {
        enga_joint *Src = &Joints[JointIndex];
        uint32 Dst = First + JointIndex;

        Assert(Src->Parent == ENGA_JOINT_NONE || Src->Parent < JointIndex);

        AppendString(&Store->JointNames[(memory_size)Dst * ENGA_MAX_ASSET_NAME], ENGA_MAX_ASSET_NAME, 0, Src->Name);
        Store->JointParent[Dst] = Src->Parent;
        CopySize(sizeof(Matrix4), Src->InverseBind, &Store->JointInverseBind[Dst]);

        transform *Bind = &Store->JointBindPose[Dst];
        Bind->Position    = Vector3(Src->Translation[0], Src->Translation[1], Src->Translation[2]);
        Bind->Orientation = Quaternion(Src->Rotation[0], Src->Rotation[1], Src->Rotation[2], Src->Rotation[3]);
        Bind->Scale       = Vector3(Src->Scale[0], Src->Scale[1], Src->Scale[2]);
    }

    AppendString(&Store->SkeletonNames[(memory_size)Handle * ENGA_MAX_ASSET_NAME], ENGA_MAX_ASSET_NAME, 0, Name);

    Store->JointUsed += JointCount;
    Store->SkeletonCount++;

    return Handle;
}

internal uint32 AssetAddAnimation(asset_store *Store, const char *Name, uint32 SkeletonHandle, real32 Duration, enga_animation_channel *Channels, uint32 ChannelCount, real32 *Floats, uint32 FloatCount)
{
    Assert(Store->AnimationCount < Store->AnimationCapacity);
    Assert(SkeletonHandle < Store->SkeletonCount);
    Assert(ChannelCount <= Store->ChannelCapacity - Store->ChannelUsed);
    Assert(FloatCount <= Store->AnimationFloatCapacity - Store->AnimationFloatUsed);

    uint32 Handle     = Store->AnimationCount;
    uint32 FirstFloat = Store->AnimationFloatUsed;
    uint32 JointCount = Store->SkeletonJointCount[SkeletonHandle];

    Store->AnimationSkeleton[Handle]     = SkeletonHandle;
    Store->AnimationDuration[Handle]     = Duration;
    Store->AnimationFirstChannel[Handle] = Store->ChannelUsed;
    Store->AnimationChannelCount[Handle] = ChannelCount;

    for (uint32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
    {
        enga_animation_channel Channel = Channels[ChannelIndex];

        uint32 ComponentCount = EngaChannelComponentCount((enga_channel_path)Channel.Path);
        Assert(Channel.Joint < JointCount && Channel.KeyCount);
        Assert(Channel.FirstTime + Channel.KeyCount <= FloatCount);
        Assert(Channel.FirstValue + Channel.KeyCount * ComponentCount <= FloatCount);

        Channel.FirstTime  += FirstFloat;
        Channel.FirstValue += FirstFloat;
        Store->Channels[Store->ChannelUsed + ChannelIndex] = Channel;
    }

    CopySize((memory_size)FloatCount * sizeof(real32), Floats, &Store->AnimationFloats[FirstFloat]);
    AppendString(&Store->AnimationNames[(memory_size)Handle * ENGA_MAX_ASSET_NAME], ENGA_MAX_ASSET_NAME, 0, Name);

    Store->ChannelUsed        += ChannelCount;
    Store->AnimationFloatUsed += FloatCount;
    Store->AnimationCount++;

    return Handle;
}

// Name lookup.

internal uint32 AssetHandleByName(char *Names, uint32 Count, const char *Name)
{
    for (uint32 Index = 0; Index < Count; ++Index)
    {
        if (StringsAreEqual(&Names[(memory_size)Index * ENGA_MAX_ASSET_NAME], Name))
        {
            return Index;
        }
    }

    return ASSET_HANDLE_NONE;
}

internal uint32 GetAssetMeshHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->MeshNames, Store->MeshCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);

    return Handle;
}

internal uint32 GetAssetTextureHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->TextureNames, Store->TextureCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);

    return Handle;
}

internal uint32 GetAssetCubemapHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->CubemapNames, Store->CubemapCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);

    return Handle;
}

internal uint32 GetAssetSkeletonHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->SkeletonNames, Store->SkeletonCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);

    return Handle;
}

internal uint32 GetAssetAnimationHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->AnimationNames, Store->AnimationCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);

    return Handle;
}

#include "AssetPack.cpp"
#include "AssetAnimation.cpp"
#include "AssetCollision.cpp"
