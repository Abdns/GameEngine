#ifndef ENGAFORMAT_H
#define ENGAFORMAT_H

#include "Types.h"

#define ENGA_MAGIC   (((uint32)'E') | ((uint32)'N' << 8) | ((uint32)'G' << 16) | ((uint32)'A' << 24))
#define ENGA_VERSION 9

#define ENGA_MAX_ASSET_NAME 32
#define ENGA_MAX_CODEPOINT  1120

#define ENGA_ASSET_DIR  "..\\EngaAsset"
#define ENGA_PACK_PATH  ENGA_ASSET_DIR "\\assets.enga"

struct enga_vertex
{
    real32 Pos[3];
    real32 Normal[3];
    real32 Color[3];
    real32 UV[2];
};

enum asset_type
{
    Asset_None = 0,
    Asset_Image,
    Asset_Sound,
    Asset_Font,
    Asset_Mesh,
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
};

enum asset_image_format
{
    ImageFormat_RGBA8 = 0,
    ImageFormat_RGBA16F,
};

inline uint32 AssetImageFormatBytes(asset_image_format Format)
{
    return (Format == ImageFormat_RGBA16F) ? 8u : 4u;
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

struct asset_font_info
{
    uint32 AtlasSize;
    uint32 CellWidth;
    uint32 CellHeight;
    real32 OriginX;
    real32 LineAdvance;
};

struct asset_descriptor
{
    uint32 Type;
    char   Name[ENGA_MAX_ASSET_NAME];
    uint64 Offset;
    uint64 Size;
    union
    {
        asset_mesh_info  Mesh;
        asset_image_info Image;
        asset_font_info  Font;
    };
};

#endif
