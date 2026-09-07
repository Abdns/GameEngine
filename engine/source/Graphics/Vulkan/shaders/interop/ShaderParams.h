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
    float  CameraNear;

    float3 LightColor;
    float  CameraFar;

    float3 CameraPos;
    float  GlobalsPad2;

    uint SkyCubemap;
    uint SkyMipCount;
    uint ScreenWidth;
    uint ScreenHeight;

    float3 VolumeCenter;
    float  GlobalsPad5;
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

struct volume_params
{
    uint  VolumeSlot;
    uint  VolumeSize;
    float VolumeSlice;
    uint  VolumeMode;

    uint  VolumeLightSlot;
    uint  VolumePad0;
    uint  VolumePad1;
    uint  VolumePad2;
};

struct volume_op_params
{
    uint SrcSlot;
    uint DstSlot;
    uint VolumeOpPad0;
    uint VolumeOpPad1;
};

struct rc_cascade_params
{
    uint Cascade;
    uint CascadePad0;
    uint CascadePad1;
    uint CascadePad2;
};

struct voxelize_params
{
    float4x4 Model;

    gpu_ptr Vertices;
    gpu_ptr Indices;

    gpu_ptr Materials;
    uint    FirstIndex;
    uint    TriangleCount;

    uint FirstVertex;
    uint MaterialSlot;
    uint VoxelizePad0;
    uint VoxelizePad1;
};

#ifndef __cplusplus
    typedef vk::BufferPointer<frame_globals, 16>     frame_globals_ptr;
    typedef vk::BufferPointer<draw_params, 16>       draw_params_ptr;
    typedef vk::BufferPointer<image_params, 16>      image_params_ptr;
    typedef vk::BufferPointer<volume_params, 16>     volume_params_ptr;
    typedef vk::BufferPointer<voxelize_params, 16>   voxelize_params_ptr;
    typedef vk::BufferPointer<volume_op_params, 16>  volume_op_params_ptr;
    typedef vk::BufferPointer<rc_cascade_params, 16> rc_cascade_params_ptr;
    typedef vk::BufferPointer<skybox_params, 16>     skybox_params_ptr;
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

    draw_params LoadDrawParams(uint64_t address)
    {
        return draw_params_ptr(address).Get();
    }

    skybox_params LoadSkyboxParams(uint64_t address)
    {
        return skybox_params_ptr(address).Get();
    }

    image_params LoadImageParams(uint64_t address)
    {
        return image_params_ptr(address).Get();
    }

    volume_params LoadVolumeParams(uint64_t address)
    {
        return volume_params_ptr(address).Get();
    }

    voxelize_params LoadVoxelizeParams(uint64_t address)
    {
        return voxelize_params_ptr(address).Get();
    }

    volume_op_params LoadVolumeOpParams(uint64_t address)
    {
        return volume_op_params_ptr(address).Get();
    }

    rc_cascade_params LoadRcCascadeParams(uint64_t address)
    {
        return rc_cascade_params_ptr(address).Get();
    }
#endif

#endif
