#pragma once

#include "Types.h"
#include "Memory.h"
#include "Strings.h"
#include "EngineMath.h"
#include "EngaFormat.h"
#include "Collider.h"

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

struct asset_pack
{
    uint8            *Data;
    uint32            Size;
    asset_descriptor *Entries;
    uint32            Count;
};

internal asset_pack AssetPackFromMemory(void *Data, uint32 Size)
{
    asset_pack Result = {};

    if (!Data || Size < sizeof(asset_file_header))
    {
        DebugLog("Asset pack is missing or too small\n");
        return Result;
    }

    asset_file_header *Header = (asset_file_header *)Data;

    uint64 TableEnd = (uint64)Header->AssetTableOffset + (uint64)Header->AssetCount * sizeof(asset_descriptor);
    if (Header->Magic != ENGA_MAGIC || Header->Version != ENGA_VERSION || TableEnd > Size)
    {
        DebugLog("Asset pack is corrupt\n");
        return Result;
    }

    Result.Data    = (uint8 *)Data;
    Result.Size    = Size;
    Result.Entries = (asset_descriptor *)((uint8 *)Data + Header->AssetTableOffset);
    Result.Count   = Header->AssetCount;

    return Result;
}

internal void *AssetPackData(asset_pack *Pack, asset_descriptor *Entry)
{
    if (Entry->Offset + Entry->Size > Pack->Size)
    {
        return 0;
    }

    return Pack->Data + Entry->Offset;
}

internal asset_store_budget AssetStoreBudgetFromPack(void *PackData, uint32 PackSize)
{
    asset_store_budget Budget = {};

    asset_pack Pack = AssetPackFromMemory(PackData, PackSize);

    for (uint32 Index = 0; Index < Pack.Count; ++Index)
    {
        asset_descriptor *Entry = &Pack.Entries[Index];

        switch ((asset_type)Entry->Type)
        {
            case Asset_Mesh:
            {
                Budget.MeshCount++;
                Budget.VertexCount += Entry->Mesh.VertexCount;
                Budget.IndexCount  += Entry->Mesh.IndexCount;
                if (Entry->Mesh.IsSkinned)
                {
                    Budget.SkinVertexCount += Entry->Mesh.VertexCount;
                }
            } break;

            case Asset_Skeleton:
            {
                Budget.SkeletonCount++;
                Budget.JointCount += Entry->Skeleton.JointCount;
            } break;

            case Asset_Animation:
            {
                Budget.AnimationCount++;
                Budget.ChannelCount        += Entry->Animation.ChannelCount;
                Budget.AnimationFloatCount += Entry->Animation.FloatCount;
            } break;

            case Asset_Image:
            {
                uint64 FormatBytes = AssetImageFormatBytes((asset_image_format)Entry->Image.Format);

                if (Entry->Image.Layers == 6)
                {
                    Budget.CubemapCount++;
                    Budget.PixelByteCount += (uint64)Entry->Image.Width * Entry->Image.Width * 6 * FormatBytes;
                }
                else
                {
                    Budget.TextureCount++;
                    Budget.PixelByteCount += (uint64)Entry->Image.Width * Entry->Image.Height * FormatBytes;
                }
            } break;

            default:
            {
            } break;
        }
    }

    return Budget;
}

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

internal uint8 *AssetTexturePixels(asset_store *Store, uint32 Handle)
{
    return Store->Pixels + Store->TextureFirstByte[Handle];
}

internal uint8 *AssetCubemapPixels(asset_store *Store, uint32 Handle)
{
    return Store->Pixels + Store->CubemapFirstByte[Handle];
}

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

internal void AssetStoreLoadPack(asset_store *Store, void *PackData, uint32 PackSize)
{
    asset_pack Pack = AssetPackFromMemory(PackData, PackSize);

    for (uint32 Index = 0; Index < Pack.Count; ++Index)
    {
        asset_descriptor *Entry = &Pack.Entries[Index];
        void *Data = AssetPackData(&Pack, Entry);
        Assert(Data);

        switch ((asset_type)Entry->Type)
        {
            case Asset_Mesh:
            {
                memory_size VertexBytes = (memory_size)Entry->Mesh.VertexCount * sizeof(enga_vertex);
                memory_size IndexBytes  = (memory_size)Entry->Mesh.IndexCount * sizeof(uint32);

                uint8 *Bytes = (uint8 *)Data;
                enga_skin_vertex *Skin = Entry->Mesh.IsSkinned ? (enga_skin_vertex *)&Bytes[VertexBytes + IndexBytes] : 0;

                AssetAddMesh(Store, Entry->Name, (enga_vertex *)Data, Entry->Mesh.VertexCount, (uint32 *)&Bytes[VertexBytes], Entry->Mesh.IndexCount, Skin);
            } break;

            case Asset_Skeleton:
            {
                AssetAddSkeleton(Store, Entry->Name, (enga_joint *)Data, Entry->Skeleton.JointCount);
            } break;

            case Asset_Animation:
            {
                uint32 SkeletonHandle = AssetHandleByName(Store->SkeletonNames, Store->SkeletonCount, Entry->Animation.Skeleton);
                if (SkeletonHandle == ASSET_HANDLE_NONE)
                {
                    DebugLog("AssetStore: animation %s refers to unknown skeleton %s\n", Entry->Name, Entry->Animation.Skeleton);
                    break;
                }

                memory_size ChannelBytes = (memory_size)Entry->Animation.ChannelCount * sizeof(enga_animation_channel);

                AssetAddAnimation(Store, Entry->Name, SkeletonHandle, Entry->Animation.Duration,
                                  (enga_animation_channel *)Data, Entry->Animation.ChannelCount,
                                  (real32 *)&((uint8 *)Data)[ChannelBytes], Entry->Animation.FloatCount);
            } break;

            case Asset_Image:
            {
                asset_image_format Format = (asset_image_format)Entry->Image.Format;

                if (Entry->Image.Layers == 6)
                {
                    AssetAddCubemap(Store, Entry->Name, Data, Entry->Image.Width, Format);
                }
                else
                {
                    AssetAddTexture(Store, Entry->Name, Data, Entry->Image.Width, Entry->Image.Height, Entry->Image.IsSRGB, Format);
                }
            } break;

            default:
            {
                DebugLog("AssetStore: asset %s of type %u has no loader\n", Entry->Name, Entry->Type);
            } break;
        }
    }

    DebugLog("AssetStore: %u meshes (%u vertices, %u indices), %u textures, %u cubemaps, %llu pixel bytes\n",
             Store->MeshCount, Store->VertexUsed, Store->IndexUsed, Store->TextureCount, Store->CubemapCount, Store->PixelByteUsed);
    DebugLog("AssetStore: %u skinned vertices, %u skeletons (%u joints), %u animations (%u channels)\n",
             Store->SkinVertexUsed, Store->SkeletonCount, Store->JointUsed, Store->AnimationCount, Store->ChannelUsed);
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

internal void AssetSampleAnimation(asset_store *Store, uint32 AnimationHandle, real32 Time, transform *Pose)
{
    uint32 Skeleton   = Store->AnimationSkeleton[AnimationHandle];
    uint32 FirstJoint = Store->SkeletonFirstJoint[Skeleton];
    uint32 JointCount = Store->SkeletonJointCount[Skeleton];

    for (uint32 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
    {
        Pose[JointIndex] = Store->JointBindPose[FirstJoint + JointIndex];
    }

    uint32 FirstChannel = Store->AnimationFirstChannel[AnimationHandle];
    uint32 ChannelCount = Store->AnimationChannelCount[AnimationHandle];
    for (uint32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
    {
        enga_animation_channel *Channel = &Store->Channels[FirstChannel + ChannelIndex];

        real32 *Times  = &Store->AnimationFloats[Channel->FirstTime];
        real32 *Values = &Store->AnimationFloats[Channel->FirstValue];
        uint32  Width  = EngaChannelComponentCount((enga_channel_path)Channel->Path);

        uint32 Next = 0;
        while (Next < Channel->KeyCount && Times[Next] <= Time)
        {
            ++Next;
        }

        uint32 A = (Next > 0) ? Next - 1 : 0;
        uint32 B = (Next < Channel->KeyCount) ? Next : Channel->KeyCount - 1;

        real32 T = 0.0f;
        if (A != B && Channel->Interpolation == ChannelInterpolation_Linear)
        {
            T = (Time - Times[A]) / (Times[B] - Times[A]);
        }

        real32 *VA = &Values[A * Width];
        real32 *VB = &Values[B * Width];

        transform *Joint = &Pose[Channel->Joint];
        switch ((enga_channel_path)Channel->Path)
        {
            case ChannelPath_Translation:
            {
                Joint->Position = Vector3(Lerp(VA[0], T, VB[0]), Lerp(VA[1], T, VB[1]), Lerp(VA[2], T, VB[2]));
            } break;

            case ChannelPath_Rotation:
            {
                Joint->Orientation = QuatNLerp(Quaternion(VA[0], VA[1], VA[2], VA[3]), Quaternion(VB[0], VB[1], VB[2], VB[3]), T);
            } break;

            case ChannelPath_Scale:
            {
                Joint->Scale = Vector3(Lerp(VA[0], T, VB[0]), Lerp(VA[1], T, VB[1]), Lerp(VA[2], T, VB[2]));
            } break;
        }
    }
}

internal void AssetSkinMatrices(asset_store *Store, uint32 SkeletonHandle, transform *Pose, Matrix4 *World, Matrix4 *Skin)
{
    uint32 FirstJoint = Store->SkeletonFirstJoint[SkeletonHandle];
    uint32 JointCount = Store->SkeletonJointCount[SkeletonHandle];

    for (uint32 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
    {
        Matrix4 Local  = Mat4FromTransform(Pose[JointIndex]);
        uint32  Parent = Store->JointParent[FirstJoint + JointIndex];

        World[JointIndex] = (Parent == ENGA_JOINT_NONE) ? Local : Mat4Multiply(World[Parent], Local);
        Skin[JointIndex]  = Mat4Multiply(World[JointIndex], Store->JointInverseBind[FirstJoint + JointIndex]);
    }
}

internal enga_vertex *AssetPoseMesh(asset_store *Store, memory_arena *Arena, uint32 MeshHandle, uint32 AnimationHandle, real32 Time)
{
    enga_skin_vertex *SkinVertices = AssetMeshSkinVertices(Store, MeshHandle);
    Assert(SkinVertices);

    uint32 Skeleton   = Store->AnimationSkeleton[AnimationHandle];
    uint32 JointCount = Store->SkeletonJointCount[Skeleton];

    transform *Pose  = PushArray(Arena, JointCount, transform);
    Matrix4   *World = PushArray(Arena, JointCount, Matrix4);
    Matrix4   *Skin  = PushArray(Arena, JointCount, Matrix4);

    AssetSampleAnimation(Store, AnimationHandle, Time, Pose);
    AssetSkinMatrices(Store, Skeleton, Pose, World, Skin);

    uint32       VertexCount = Store->MeshVertexCount[MeshHandle];
    enga_vertex *Source      = AssetMeshVertices(Store, MeshHandle);
    enga_vertex *Result      = PushArray(Arena, VertexCount, enga_vertex);

    for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        enga_vertex      *Src     = &Source[VertexIndex];
        enga_skin_vertex *Weights = &SkinVertices[VertexIndex];

        Vector3 P = Vector3(Src->Pos[0], Src->Pos[1], Src->Pos[2]);
        Vector3 N = Vector3(Src->Normal[0], Src->Normal[1], Src->Normal[2]);

        Vector3 PosedP = Vector3(0.0f, 0.0f, 0.0f);
        Vector3 PosedN = Vector3(0.0f, 0.0f, 0.0f);
        for (uint32 Influence = 0; Influence < ENGA_MAX_SKIN_INFLUENCES; ++Influence)
        {
            real32 Weight = Weights->Weights[Influence];
            uint32 Joint  = Weights->Joints[Influence];
            if (Weight <= 0.0f || Joint >= JointCount)
            {
                continue;
            }

            PosedP += Weight * Mat4Transform(Skin[Joint], P, 1.0f);
            PosedN += Weight * Mat4Transform(Skin[Joint], N, 0.0f);
        }
        PosedN = Normalize(PosedN);

        enga_vertex *Dst = &Result[VertexIndex];
        *Dst = *Src;
        Dst->Pos[0]    = PosedP.X;
        Dst->Pos[1]    = PosedP.Y;
        Dst->Pos[2]    = PosedP.Z;
        Dst->Normal[0] = PosedN.X;
        Dst->Normal[1] = PosedN.Y;
        Dst->Normal[2] = PosedN.Z;
    }

    return Result;
}
