#ifndef SHADERINTEROP_H
#define SHADERINTEROP_H

#define SET_GLOBAL 0

#define BINDING_TEXTURES        0
#define BINDING_SAMPLER         1
#define BINDING_CUBEMAPS        2
#define BINDING_VOLUMES         3
#define BINDING_STORAGE_VOLUMES 4
#define BINDING_UINT_VOLUMES    5

#define MAX_TEXTURES  32
#define MAX_CUBEMAPS  4
#define MAX_VOLUMES      32
#define MAX_UINT_VOLUMES 2

#define UINT_SLOT_ALBEDO 0
#define UINT_SLOT_NORMAL 1
#define MAX_MATERIALS 64

#define VOLUME_SLOT_ALBEDO        0
#define VOLUME_SLOT_NORMAL        1
#define VOLUME_SLOT_LIGHT_HISTORY 2
#define VOLUME_SLOT_SKYVIS        8
#define VOLUME_SLOT_SKYVIS_SCRATCH 9
#define VOLUME_SLOT_SEED_A        10
#define VOLUME_SLOT_SEED_B        11
#define VOLUME_SLOT_SDF           12

#define LIGHT_DIRECTIONS 6
#define LIGHT_BLEND      0.1
#define LIGHT_GI_STRENGTH  1.0
#define LIGHT_SKY_STRENGTH 1.0
#define LIGHT_READ_AMBIENT 0.1
#define LIGHT_BOUNCE_STRENGTH 1.0

#define PROBE_RAYS_PER_FRAME 8
#define PROBE_RAY_SET        64
#define PROBE_MARCH_STEPS    48

#define VOLUME_GROUP_SIZE 4
#define VOXEL_GROUP_SIZE  64

#define VOXEL_GRID_SIZE    128
#define LIGHT_GRID_SIZE    64
#define VOXEL_WORLD_EXTENT 12.0f

#define VOLUME_MODE_SLICE  0
#define VOLUME_MODE_COLUMN 1
#define VOLUME_MODE_CAMERA 2
#define VOLUME_MODE_LIGHT  3

#define VOLUME_MARCH_STEPS 512

#define TEXTURE_NONE 0xFFFFFFFF

#define TEXTURE_SLOT_SCENE MAX_TEXTURES
#define TEXTURE_SLOT_POST  (TEXTURE_SLOT_SCENE + 1)
#define TEXTURE_HEAP_SIZE  (TEXTURE_SLOT_POST + 1)

#define VERTEX_STRIDE    44
#define MATERIAL_STRIDE  32
#define RECT_PARAMS_STRIDE 64

#ifdef __cplusplus
#define float4x4 Matrix4
#define float4   Vector4
#define float3   Vector3
#define float2   Vector2
#define uint     uint32
#define gpu_ptr  uint64
#else
#define gpu_ptr  uint64_t
#endif

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

    uint SkyCubemap;
    uint SkyMipCount;
    uint FrameIndex;
    uint GlobalsPad4;

    float3 VoxelCenter;
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

struct flood_params
{
    uint SourceSlot;
    uint TargetSlot;
    uint GridSize;
    uint StepSize;
};

struct trace_params
{
    uint LightSlot;
    uint LightSize;
    uint SdfSlot;
    uint TracePad0;
};

struct downsample_params
{
    uint AlbedoSlot;
    uint NormalSlot;
    uint SolidSlot;
    uint LightNormalSlot;

    uint VoxelSize;
    uint LightSize;
    uint DownsamplePad0;
    uint DownsamplePad1;
};

struct voxelize_params
{
    float4x4 Model;

    gpu_ptr Vertices;
    gpu_ptr Indices;

    gpu_ptr Materials;
    uint    FirstIndex;
    uint    TriangleCount;

    float3 GridCenter;
    float  GridExtent;

    uint FirstVertex;
    uint MaterialSlot;
    uint VolumeSlot;
    uint GridSize;

    uint NormalSlot;
    uint VoxelizePad0;
    uint VoxelizePad1;
    uint VoxelizePad2;
};

#ifndef __cplusplus
    typedef vk::BufferPointer<frame_globals, 16>     frame_globals_ptr;
    typedef vk::BufferPointer<draw_params, 16>       draw_params_ptr;
    typedef vk::BufferPointer<image_params, 16>      image_params_ptr;
    typedef vk::BufferPointer<volume_params, 16>     volume_params_ptr;
    typedef vk::BufferPointer<voxelize_params, 16>   voxelize_params_ptr;
    typedef vk::BufferPointer<downsample_params, 16> downsample_params_ptr;
    typedef vk::BufferPointer<flood_params, 16>      flood_params_ptr;
    typedef vk::BufferPointer<trace_params, 16>      trace_params_ptr;

    static const float3 LightAxis[LIGHT_DIRECTIONS] =
    {
        float3( 1.0,  0.0,  0.0),
        float3(-1.0,  0.0,  0.0),
        float3( 0.0,  1.0,  0.0),
        float3( 0.0, -1.0,  0.0),
        float3( 0.0,  0.0,  1.0),
        float3( 0.0,  0.0, -1.0),
    };
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

    downsample_params LoadDownsampleParams(uint64_t address)
    {
        return downsample_params_ptr(address).Get();
    }

    flood_params LoadFloodParams(uint64_t address)
    {
        return flood_params_ptr(address).Get();
    }

    trace_params LoadTraceParams(uint64_t address)
    {
        return trace_params_ptr(address).Get();
    }
#endif

#ifdef __cplusplus
    #undef float4x4
    #undef float4
    #undef float3
    #undef float2
    #undef uint
    #undef gpu_ptr
#endif

#endif
