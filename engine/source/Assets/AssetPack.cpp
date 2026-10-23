#pragma once

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
