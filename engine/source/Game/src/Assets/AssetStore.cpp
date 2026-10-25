#pragma once

#include "Types.h"
#include "Memory.h"
#include "Strings.h"
#include "EngineMath.h"
#include "EngaFormat.h"

#define ASSET_HANDLE_NONE 0xFFFFFFFF

struct loaded_mesh
{
    char Name[ENGA_MAX_ASSET_NAME];
    uint32 VertexCount;
    uint32 IndexCount;
    enga_vertex      *Vertices;
    uint32           *Indices;
    enga_skin_vertex *SkinVertices;
    Vector3 BoundsMin;
    Vector3 BoundsMax;
};

struct loaded_image
{
    char Name[ENGA_MAX_ASSET_NAME];
    void *Pixels;
    uint32 Width;
    uint32 Height;
    bool32 SRGB;
    image_format Format;
};

struct loaded_joint
{
    char Name[ENGA_MAX_ASSET_NAME];
    uint32 Parent;
    Matrix4 InverseBind;
    transform BindPose;
};

struct loaded_skeleton
{
    char Name[ENGA_MAX_ASSET_NAME];
    uint32 JointCount;
    loaded_joint *Joints;
};

struct loaded_animation
{
    char Name[ENGA_MAX_ASSET_NAME];
    uint32 SkeletonHandle;
    real32 Duration;
    uint32 ChannelCount;
    enga_animation_channel *Channels;
    real32 *Floats;
};

struct asset_store
{
    loaded_mesh      *Meshes;
    loaded_image     *Textures;
    loaded_image     *Cubemaps;
    loaded_skeleton  *Skeletons;
    loaded_animation *Animations;

    uint32 MeshCount;
    uint32 TextureCount;
    uint32 CubemapCount;
    uint32 SkeletonCount;
    uint32 AnimationCount;
};

inline Vector3 EngaVertexPosition(enga_vertex *Vertex)
{
    return Vector3(Vertex->Pos[0], Vertex->Pos[1], Vertex->Pos[2]);
}

template <typename asset>
internal uint32 AssetHandleByName(asset *Assets, uint32 Count, const char *Name)
{
    for (uint32 Index = 0; Index < Count; ++Index)
    {
        if (StringsAreEqual(Assets[Index].Name, Name))
        {
            return Index;
        }
    }

    return ASSET_HANDLE_NONE;
}

internal uint32 GetAssetMeshHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->Meshes, Store->MeshCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);
    return Handle;
}

internal uint32 GetAssetTextureHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->Textures, Store->TextureCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);
    return Handle;
}

internal uint32 GetAssetCubemapHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->Cubemaps, Store->CubemapCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);
    return Handle;
}

internal uint32 GetAssetSkeletonHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->Skeletons, Store->SkeletonCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);
    return Handle;
}

internal uint32 GetAssetAnimationHandle(asset_store *Store, const char *Name)
{
    uint32 Handle = AssetHandleByName(Store->Animations, Store->AnimationCount, Name);
    Assert(Handle != ASSET_HANDLE_NONE);
    return Handle;
}

internal void PushAssetsToRender(asset_store* Assets, render_commands* Commands)
{
    for (uint32 Handle = 0; Handle < Assets->MeshCount; ++Handle)
    {
        loaded_mesh* Mesh = &Assets->Meshes[Handle];
        PushLoadMesh(Commands, Handle, Mesh->Vertices, Mesh->VertexCount, Mesh->Indices, Mesh->IndexCount);
    }

    for (uint32 Handle = 0; Handle < Assets->TextureCount; ++Handle)
    {
        loaded_image* Texture = &Assets->Textures[Handle];
        PushLoadTexture(Commands, Handle, Texture->Pixels, Texture->Width, Texture->Height, Texture->SRGB, Texture->Format);
    }

    for (uint32 Handle = 0; Handle < Assets->CubemapCount; ++Handle)
    {
        loaded_image* Cubemap = &Assets->Cubemaps[Handle];
        PushLoadCubemap(Commands, Handle, Cubemap->Pixels, Cubemap->Width, Cubemap->Format);
    }
}

#include "AssetPack.cpp"
#include "AssetAnimation.cpp"
