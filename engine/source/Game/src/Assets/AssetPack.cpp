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
    if (Entry->Offset > Pack->Size || Entry->Size > Pack->Size - Entry->Offset)
    {
        return 0;
    }

    return Pack->Data + Entry->Offset;
}

internal void *CopyAssetData(memory_arena *Arena, void *Data, memory_size Size)
{
    void *Result = PushSize(Arena, Size);
    CopySize(Size, Data, Result);
    return Result;
}

internal void AssetStoreLoadPack(asset_store *Store, memory_arena *Arena, void *PackData, uint32 PackSize)
{
    ZeroStruct(*Store);
    asset_pack Pack = AssetPackFromMemory(PackData, PackSize);

    // Count only asset records; each payload is allocated when it is loaded.
    for (uint32 Index = 0; Index < Pack.Count; ++Index)
    {
        asset_descriptor *Entry = &Pack.Entries[Index];
        switch ((asset_type)Entry->Type)
        {
            case Asset_Mesh:      ++Store->MeshCount;      break;
            case Asset_Skeleton:  ++Store->SkeletonCount;  break;
            case Asset_Animation: ++Store->AnimationCount; break;
            case Asset_Image:
            {
                if (Entry->Image.Layers == 6) ++Store->CubemapCount;
                else                         ++Store->TextureCount;
            } break;
        }
    }

    Store->Meshes     = PushArrayAligned(Arena, Store->MeshCount, loaded_mesh, alignof(loaded_mesh));
    Store->Textures   = PushArrayAligned(Arena, Store->TextureCount, loaded_image, alignof(loaded_image));
    Store->Cubemaps   = PushArrayAligned(Arena, Store->CubemapCount, loaded_image, alignof(loaded_image));
    Store->Skeletons  = PushArrayAligned(Arena, Store->SkeletonCount, loaded_skeleton, alignof(loaded_skeleton));
    Store->Animations = PushArrayAligned(Arena, Store->AnimationCount, loaded_animation, alignof(loaded_animation));
    Store->MeshCount = Store->TextureCount = Store->CubemapCount = Store->SkeletonCount = Store->AnimationCount = 0;

    for (uint32 Index = 0; Index < Pack.Count; ++Index)
    {
        asset_descriptor *Entry = &Pack.Entries[Index];
        void *Data = AssetPackData(&Pack, Entry);
        Assert(Data);
        if (!Data) continue;

        switch ((asset_type)Entry->Type)
        {
            case Asset_Mesh:
            {
                Assert(Entry->Mesh.VertexCount && Entry->Mesh.IndexCount);
                memory_size ByteSize = AssetMeshBytes(Entry->Mesh);
                Assert(ByteSize <= Entry->Size);
                if (ByteSize > Entry->Size) break;

                loaded_mesh *Mesh = &Store->Meshes[Store->MeshCount++];
                *Mesh = {};
                AppendString(Mesh->Name, sizeof(Mesh->Name), 0, Entry->Name);
                Mesh->VertexCount = Entry->Mesh.VertexCount;
                Mesh->IndexCount  = Entry->Mesh.IndexCount;

                uint8 *Bytes = (uint8 *)CopyAssetData(Arena, Data, ByteSize);
                Mesh->Vertices = (enga_vertex *)Bytes;
                Mesh->Indices  = (uint32 *)(Bytes + (memory_size)Mesh->VertexCount * sizeof(enga_vertex));
                if (Entry->Mesh.IsSkinned)
                {
                    Mesh->SkinVertices = (enga_skin_vertex *)(Mesh->Indices + Mesh->IndexCount);
                }

                Mesh->BoundsMin = Vector3( REAL32_LARGE,  REAL32_LARGE,  REAL32_LARGE);
                Mesh->BoundsMax = Vector3(-REAL32_LARGE, -REAL32_LARGE, -REAL32_LARGE);
                for (uint32 VertexIndex = 0; VertexIndex < Mesh->VertexCount; ++VertexIndex)
                {
                    Vector3 P = EngaVertexPosition(&Mesh->Vertices[VertexIndex]);
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        Mesh->BoundsMin.Elements[Axis] = Minimum(Mesh->BoundsMin.Elements[Axis], P.Elements[Axis]);
                        Mesh->BoundsMax.Elements[Axis] = Maximum(Mesh->BoundsMax.Elements[Axis], P.Elements[Axis]);
                    }
                }
            } break;

            case Asset_Image:
            {
                bool32 IsCubemap = Entry->Image.Layers == 6;
                uint32 Height = IsCubemap ? Entry->Image.Width : Entry->Image.Height;
                image_format Format = (image_format)Entry->Image.Format;
                memory_size ByteSize = (memory_size)Entry->Image.Width * Height * (IsCubemap ? 6u : 1u) * ImageFormatBytes(Format);
                Assert(ByteSize <= Entry->Size);
                if (ByteSize > Entry->Size) break;

                loaded_image *Image = IsCubemap ? &Store->Cubemaps[Store->CubemapCount++] : &Store->Textures[Store->TextureCount++];
                *Image = {};
                AppendString(Image->Name, sizeof(Image->Name), 0, Entry->Name);
                Image->Pixels = CopyAssetData(Arena, Data, ByteSize);
                Image->Width  = Entry->Image.Width;
                Image->Height = Height;
                Image->SRGB   = Entry->Image.IsSRGB;
                Image->Format = Format;
            } break;

            case Asset_Skeleton:
            {
                Assert(Entry->Skeleton.JointCount);
                memory_size ByteSize = (memory_size)Entry->Skeleton.JointCount * sizeof(enga_joint);
                Assert(ByteSize <= Entry->Size);
                if (ByteSize > Entry->Size) break;

                loaded_skeleton *Skeleton = &Store->Skeletons[Store->SkeletonCount++];
                *Skeleton = {};
                AppendString(Skeleton->Name, sizeof(Skeleton->Name), 0, Entry->Name);
                Skeleton->JointCount = Entry->Skeleton.JointCount;
                Skeleton->Joints = PushArray(Arena, Skeleton->JointCount, loaded_joint);

                enga_joint *Source = (enga_joint *)Data;
                for (uint32 JointIndex = 0; JointIndex < Skeleton->JointCount; ++JointIndex)
                {
                    enga_joint *Src = &Source[JointIndex];
                    loaded_joint *Joint = &Skeleton->Joints[JointIndex];
                    Assert(Src->Parent == ENGA_JOINT_NONE || Src->Parent < JointIndex);

                    AppendString(Joint->Name, sizeof(Joint->Name), 0, Src->Name);
                    Joint->Parent = Src->Parent;
                    CopySize(sizeof(Matrix4), Src->InverseBind, &Joint->InverseBind);
                    Joint->BindPose.Position    = Vector3(Src->Translation[0], Src->Translation[1], Src->Translation[2]);
                    Joint->BindPose.Orientation = Quaternion(Src->Rotation[0], Src->Rotation[1], Src->Rotation[2], Src->Rotation[3]);
                    Joint->BindPose.Scale       = Vector3(Src->Scale[0], Src->Scale[1], Src->Scale[2]);
                }
            } break;

            case Asset_Animation: break; // Resolve skeleton references after all skeletons are loaded.

            default:
            {
                DebugLog("AssetStore: asset %s of type %u has no loader\n", Entry->Name, Entry->Type);
            } break;
        }
    }

    for (uint32 Index = 0; Index < Pack.Count; ++Index)
    {
        asset_descriptor *Entry = &Pack.Entries[Index];
        if (Entry->Type != Asset_Animation) continue;

        uint32 SkeletonHandle = AssetHandleByName(Store->Skeletons, Store->SkeletonCount, Entry->Animation.Skeleton);
        if (SkeletonHandle == ASSET_HANDLE_NONE)
        {
            DebugLog("AssetStore: animation %s refers to unknown skeleton %s\n", Entry->Name, Entry->Animation.Skeleton);
            continue;
        }

        void *Data = AssetPackData(&Pack, Entry);
        memory_size ByteSize = AssetAnimationBytes(Entry->Animation);
        Assert(Data && ByteSize <= Entry->Size);
        if (!Data || ByteSize > Entry->Size) continue;

        loaded_animation *Animation = &Store->Animations[Store->AnimationCount++];
        *Animation = {};
        AppendString(Animation->Name, sizeof(Animation->Name), 0, Entry->Name);
        Animation->SkeletonHandle = SkeletonHandle;
        Animation->Duration = Entry->Animation.Duration;
        Animation->ChannelCount = Entry->Animation.ChannelCount;
        Animation->Channels = (enga_animation_channel *)CopyAssetData(Arena, Data, ByteSize);
        Animation->Floats = (real32 *)(Animation->Channels + Animation->ChannelCount);

        // Channel offsets stay relative to this animation's own float array.
        uint32 JointCount = Store->Skeletons[SkeletonHandle].JointCount;
        for (uint32 ChannelIndex = 0; ChannelIndex < Animation->ChannelCount; ++ChannelIndex)
        {
            enga_animation_channel *Channel = &Animation->Channels[ChannelIndex];
            uint32 Width = EngaChannelComponentCount((enga_channel_path)Channel->Path);
            Assert(Channel->Joint < JointCount && Channel->KeyCount);
            Assert((uint64)Channel->FirstTime + Channel->KeyCount <= Entry->Animation.FloatCount);
            Assert((uint64)Channel->FirstValue + (uint64)Channel->KeyCount * Width <= Entry->Animation.FloatCount);
        }
    }

    DebugLog("AssetStore: %u meshes, %u textures, %u cubemaps, %u skeletons, %u animations\n",
             Store->MeshCount, Store->TextureCount, Store->CubemapCount, Store->SkeletonCount, Store->AnimationCount);
}
