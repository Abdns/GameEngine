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
    uint32 FontCount;
    uint32 VertexCount;
    uint32 IndexCount;
    uint64 PixelByteCount;
};

struct asset_store
{
    enga_vertex *Vertices;
    uint32      *Indices;
    uint8       *Pixels;

    uint32 VertexUsed, VertexCapacity;
    uint32 IndexUsed,  IndexCapacity;
    uint64 PixelByteUsed, PixelByteCapacity;

    char    *MeshNames;
    uint32  *MeshFirstVertex;
    uint32  *MeshVertexCount;
    uint32  *MeshFirstIndex;
    uint32  *MeshIndexCount;
    Vector3 *MeshBoundsMin;
    Vector3 *MeshBoundsMax;
    uint32   MeshCount, MeshCapacity;

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

    char   *FontNames;
    asset_font_info *FontInfo;
    uint32 *FontTextureHandle;
    uint16 *FontMap;
    real32 *FontAdvance;
    uint32  FontCount, FontCapacity;

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
        asset_descriptor *Entry = Pack.Entries + Index;

        switch ((asset_type)Entry->Type)
        {
            case Asset_Mesh:
            {
                Budget.MeshCount++;
                Budget.VertexCount += Entry->Mesh.VertexCount;
                Budget.IndexCount  += Entry->Mesh.IndexCount;
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

            case Asset_Font:
            {
                Budget.FontCount++;
                Budget.TextureCount++;
                Budget.PixelByteCount += (uint64)Entry->Font.AtlasSize * Entry->Font.AtlasSize * 4;
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
    Result.FontCount      = A.FontCount      + B.FontCount;
    Result.VertexCount    = A.VertexCount    + B.VertexCount;
    Result.IndexCount     = A.IndexCount     + B.IndexCount;
    Result.PixelByteCount = A.PixelByteCount + B.PixelByteCount;

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
    Store->FontCapacity      = Budget.FontCount;

    if (Store->VertexCapacity)
    {
        Store->Vertices = PushArray(Arena, Store->VertexCapacity, enga_vertex);
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

    if (Store->FontCapacity)
    {
        Store->FontNames         = PushArray(Arena, (memory_size)Store->FontCapacity * ENGA_MAX_ASSET_NAME, char);
        Store->FontInfo          = PushArray(Arena, Store->FontCapacity, asset_font_info);
        Store->FontTextureHandle = PushArray(Arena, Store->FontCapacity, uint32);
        Store->FontMap           = PushArray(Arena, (memory_size)Store->FontCapacity * ENGA_MAX_CODEPOINT, uint16);
        Store->FontAdvance       = PushArray(Arena, (memory_size)Store->FontCapacity * ENGA_MAX_CODEPOINT, real32);
    }
}

inline Vector3 EngaVertexPosition(enga_vertex *Vertex)
{
    return Vector3(Vertex->Pos[0], Vertex->Pos[1], Vertex->Pos[2]);
}

internal enga_vertex *AssetMeshVertices(asset_store *Store, uint32 Handle)
{
    return Store->Vertices + Store->MeshFirstVertex[Handle];
}

internal uint32 *AssetMeshIndices(asset_store *Store, uint32 Handle)
{
    return Store->Indices + Store->MeshFirstIndex[Handle];
}

internal uint8 *AssetTexturePixels(asset_store *Store, uint32 Handle)
{
    return Store->Pixels + Store->TextureFirstByte[Handle];
}

internal uint8 *AssetCubemapPixels(asset_store *Store, uint32 Handle)
{
    return Store->Pixels + Store->CubemapFirstByte[Handle];
}

internal uint16 *AssetFontMap(asset_store *Store, uint32 Handle)
{
    return Store->FontMap + (memory_size)Handle * ENGA_MAX_CODEPOINT;
}

internal real32 *AssetFontAdvance(asset_store *Store, uint32 Handle)
{
    return Store->FontAdvance + (memory_size)Handle * ENGA_MAX_CODEPOINT;
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

internal uint32 AssetAddMesh(asset_store *Store, const char *Name, enga_vertex *Vertices, uint32 VertexCount, uint32 *Indices, uint32 IndexCount)
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

    Vector3 BoundsMin = Vector3( REAL32_LARGE,  REAL32_LARGE,  REAL32_LARGE);
    Vector3 BoundsMax = Vector3(-REAL32_LARGE, -REAL32_LARGE, -REAL32_LARGE);
    for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        Vector3 P = EngaVertexPosition(Vertices + VertexIndex);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            BoundsMin.Elements[Axis] = Minimum(BoundsMin.Elements[Axis], P.Elements[Axis]);
            BoundsMax.Elements[Axis] = Maximum(BoundsMax.Elements[Axis], P.Elements[Axis]);
        }
    }
    Store->MeshBoundsMin[Handle] = BoundsMin;
    Store->MeshBoundsMax[Handle] = BoundsMax;

    AppendString(Store->MeshNames + (memory_size)Handle * ENGA_MAX_ASSET_NAME, ENGA_MAX_ASSET_NAME, 0, Name);

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

    AppendString(Store->TextureNames + (memory_size)Handle * ENGA_MAX_ASSET_NAME, ENGA_MAX_ASSET_NAME, 0, Name);
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

    AppendString(Store->CubemapNames + (memory_size)Handle * ENGA_MAX_ASSET_NAME, ENGA_MAX_ASSET_NAME, 0, Name);
    Store->CubemapFaceSize[Handle] = FaceSize;
    Store->CubemapFormat[Handle]   = (uint32)Format;
    Store->CubemapCount++;
    Store->PixelByteUsed += ByteSize;

    return Handle;
}

internal uint32 AssetAddFont(asset_store *Store, const char *Name, asset_font_info *Info, uint16 *Map, real32 *Advances, void *AtlasPixels)
{
    Assert(Store->FontCount < Store->FontCapacity);

    uint32 TextureHandle = AssetAddTexture(Store, Name, AtlasPixels, Info->AtlasSize, Info->AtlasSize, false, ImageFormat_RGBA8);

    uint32 Handle = Store->FontCount;

    CopySize(ENGA_MAX_CODEPOINT * sizeof(uint16), Map, AssetFontMap(Store, Handle));
    CopySize(ENGA_MAX_CODEPOINT * sizeof(real32), Advances, AssetFontAdvance(Store, Handle));

    AppendString(Store->FontNames + (memory_size)Handle * ENGA_MAX_ASSET_NAME, ENGA_MAX_ASSET_NAME, 0, Name);
    Store->FontInfo[Handle]          = *Info;
    Store->FontTextureHandle[Handle] = TextureHandle;

    Store->FontCount++;

    return Handle;
}

internal void AssetStoreLoadPack(asset_store *Store, void *PackData, uint32 PackSize)
{
    asset_pack Pack = AssetPackFromMemory(PackData, PackSize);

    for (uint32 Index = 0; Index < Pack.Count; ++Index)
    {
        asset_descriptor *Entry = Pack.Entries + Index;
        void *Data = AssetPackData(&Pack, Entry);
        Assert(Data);

        switch ((asset_type)Entry->Type)
        {
            case Asset_Mesh:
            {
                memory_size VertexBytes = (memory_size)Entry->Mesh.VertexCount * sizeof(enga_vertex);

                AssetAddMesh(Store, Entry->Name, (enga_vertex *)Data, Entry->Mesh.VertexCount, (uint32 *)((uint8 *)Data + VertexBytes), Entry->Mesh.IndexCount);
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

            case Asset_Font:
            {
                memory_size MapBytes     = ENGA_MAX_CODEPOINT * sizeof(uint16);
                memory_size AdvanceBytes = ENGA_MAX_CODEPOINT * sizeof(real32);

                AssetAddFont(Store, Entry->Name, &Entry->Font, (uint16 *)Data, (real32 *)((uint8 *)Data + MapBytes), (uint8 *)Data + MapBytes + AdvanceBytes);
            } break;

            default:
            {
                DebugLog("AssetStore: asset %s of type %u has no loader\n", Entry->Name, Entry->Type);
            } break;
        }
    }

    DebugLog("AssetStore: %u meshes (%u vertices, %u indices), %u textures, %u cubemaps, %u fonts, %llu pixel bytes\n",
             Store->MeshCount, Store->VertexUsed, Store->IndexUsed, Store->TextureCount, Store->CubemapCount, Store->FontCount, Store->PixelByteUsed);
}

internal uint32 AssetHandleByName(char *Names, uint32 Count, const char *Name)
{
    for (uint32 Index = 0; Index < Count; ++Index)
    {
        if (StringsAreEqual(Names + (memory_size)Index * ENGA_MAX_ASSET_NAME, Name))
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

internal uint32 GetAssetFontHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->FontNames, Store->FontCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);

    return Handle;
}
