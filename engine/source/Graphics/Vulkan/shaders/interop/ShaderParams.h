#ifndef SHADERPARAMS_H
#define SHADERPARAMS_H

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

    float3 LightColor;
    float  GlobalsPad1;

    float3 CameraPos;
    float  GlobalsPad2;
};

struct gpu_material
{
    float4 BaseColor;
    uint   TextureSlot;
    float  Metallic;
    float  Roughness;
    uint   MaterialPad0;
};

struct draw_params
{
    float4x4 Model;
    float4   Tint;

    gpu_ptr Vertices;
    gpu_ptr Materials;

    uint MaterialSlot;
    uint DrawPad0;
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

#ifndef __cplusplus
    typedef vk::BufferPointer<frame_globals, 16>     frame_globals_ptr;
    typedef vk::BufferPointer<rect_params, 16>       rect_params_ptr;

    vertex LoadVertex(uint64_t base, uint index)
    {
        return vk::RawBufferLoad<vertex>(base + (uint64_t)index * VERTEX_STRIDE, 4);
    }

    gpu_material LoadMaterial(uint64_t base, uint slot)
    {
        return vk::RawBufferLoad<gpu_material>(base + (uint64_t)slot * MATERIAL_STRIDE, 16);
    }

    uint LoadIndex(uint64_t base, uint index)
    {
        return vk::RawBufferLoad<uint>(base + (uint64_t)index * 4, 4);
    }

    rect_params LoadRect(uint64_t base, uint index)
    {
        return rect_params_ptr(base + (uint64_t)index * RECT_PARAMS_STRIDE).Get();
    }

    frame_globals LoadGlobals(uint64_t address)
    {
        return frame_globals_ptr(address).Get();
    }

    #define LoadPassParams(Type) vk::BufferPointer<Type, 16>(pc.ParamsPtr).Get()
#endif

#endif
