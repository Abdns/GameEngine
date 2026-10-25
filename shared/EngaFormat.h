#ifndef ENGAFORMAT_H
#define ENGAFORMAT_H

#include "Types.h"
#include "ImageFormat.h"

#define ENGA_MAGIC   (((uint32)'E') | ((uint32)'N' << 8) | ((uint32)'G' << 16) | ((uint32)'A' << 24))
#define ENGA_VERSION 11

#define ENGA_MAX_ASSET_NAME 32

#define ENGA_MAX_SKIN_INFLUENCES 4
#define ENGA_JOINT_NONE          0xFFFFFFFF

#define ENGA_ASSET_DIR  "..\\EngaAsset"
#define ENGA_PACK_PATH  ENGA_ASSET_DIR "\\assets.enga"

struct enga_vertex
{
    real32 Pos[3];
    real32 Normal[3];
    real32 Color[3];
    real32 UV[2];
};

struct enga_skin_vertex
{
    uint16 Joints[ENGA_MAX_SKIN_INFLUENCES];
    real32 Weights[ENGA_MAX_SKIN_INFLUENCES];
};

struct enga_joint
{
    char   Name[ENGA_MAX_ASSET_NAME];
    uint32 Parent;
    real32 InverseBind[16];
    real32 Translation[3];
    real32 Rotation[4];
    real32 Scale[3];
};

enum enga_channel_path
{
    ChannelPath_Translation = 0,
    ChannelPath_Rotation,
    ChannelPath_Scale,
};

enum enga_channel_interpolation
{
    ChannelInterpolation_Linear = 0,
    ChannelInterpolation_Step,
};

struct enga_animation_channel
{
    uint32 Joint;
    uint32 Path;
    uint32 Interpolation;
    uint32 KeyCount;
    uint32 FirstTime;
    uint32 FirstValue;
};

inline uint32 EngaChannelComponentCount(enga_channel_path Path)
{
    return (Path == ChannelPath_Rotation) ? 4u : 3u;
}

enum asset_type
{
    Asset_None = 0,
    Asset_Image,
    Asset_Sound,
    Asset_Mesh,
    Asset_Skeleton,
    Asset_Animation,
};

struct asset_file_header
{
    uint32 Magic;
    uint32 Version;
    uint32 AssetCount;
    uint32 AssetTableOffset;
};

struct asset_mesh_info
{
    uint32 VertexCount;
    uint32 IndexCount;
    bool32 IsSkinned;
};

inline uint64 AssetMeshBytes(asset_mesh_info Mesh)
{
    uint64 Result = (uint64)Mesh.VertexCount * sizeof(enga_vertex) + (uint64)Mesh.IndexCount * sizeof(uint32);
    if (Mesh.IsSkinned)
    {
        Result += (uint64)Mesh.VertexCount * sizeof(enga_skin_vertex);
    }

    return Result;
}

struct asset_skeleton_info
{
    uint32 JointCount;
};

struct asset_animation_info
{
    char   Skeleton[ENGA_MAX_ASSET_NAME];
    real32 Duration;
    uint32 ChannelCount;
    uint32 FloatCount;
};

inline uint64 AssetAnimationBytes(asset_animation_info Animation)
{
    return (uint64)Animation.ChannelCount * sizeof(enga_animation_channel) + (uint64)Animation.FloatCount * sizeof(real32);
}

struct asset_image_info
{
    uint32 Format;
    bool32 IsSRGB;
    bool32 IsAtlas;
    uint32 Width;
    uint32 Height;
    uint32 Layers;
};

struct asset_descriptor
{
    uint32 Type;
    char   Name[ENGA_MAX_ASSET_NAME];
    uint64 Offset;
    uint64 Size;
    union
    {
        asset_mesh_info      Mesh;
        asset_image_info     Image;
        asset_skeleton_info  Skeleton;
        asset_animation_info Animation;
    };
};

#endif
