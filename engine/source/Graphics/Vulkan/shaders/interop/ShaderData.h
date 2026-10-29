#ifndef SHADERDATA_H
#define SHADERDATA_H

#ifdef __cplusplus
#include "Types.h"
#include "EngineMath.h"

#define float4x4 Matrix4
#define float4   Vector4
#define float3   Vector3
#define float2   Vector2
#define uint     uint32
#define gpu_ptr  uint64
#else
#define gpu_ptr  uint64_t
#endif

#define VERTEX_STRIDE      44
#define RECT_PARAMS_STRIDE 64

struct push_constants
{
    gpu_ptr ParamsPtr;
    gpu_ptr GlobalsPtr;
};

struct vertex
{
    float3 Position;
    float3 Normal;
    float3 Color;
    float2 UV;
};

struct frame_globals
{
    float4x4 ViewProj;

    float4 SkyRight;
    float4 SkyUp;
    float4 SkyForward;

    float3 LightDir;
    float  GlobalsPad0;
};

struct gpu_material_params
{
    float4 Color;
    gpu_ptr TextureHandles;
    uint TextureCount;
    uint MaterialPad0;
};

struct draw_params
{
    float4x4 Model;
    gpu_material_params Material;
    gpu_ptr Vertices;
    uint DrawPad0;
    uint DrawPad1;
};

struct skybox_params
{
    float4 Tint;

    uint CubemapIndex;
    uint SkyboxPad0;
};

struct rect_params
{
    float4 Rect;
    float4 UVRect;
    float4 Tint;

    uint TextureSlot;
    uint RectPad0;
    uint RectPad1;
    uint RectPad2;
};

struct image_params
{
    uint TextureSlot;
    uint ImagePad0;
    uint ImagePad1;
    uint ImagePad2;
};

#ifdef __cplusplus
#undef float4x4
#undef float4
#undef float3
#undef float2
#undef uint
#endif
#undef gpu_ptr

#endif
