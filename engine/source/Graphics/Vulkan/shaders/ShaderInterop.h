#ifndef SHADERINTEROP_H
#define SHADERINTEROP_H

#define SET_GLOBAL 0

#define BINDING_TEXTURES        0
#define BINDING_SAMPLER         1
#define BINDING_CUBEMAPS        2
#define BINDING_VOLUMES         3
#define BINDING_STORAGE_VOLUMES 4
#define BINDING_UINT_VOLUMES    5
#define BINDING_VOLUME_SAMPLER  6

#define MAX_TEXTURES  32
#define MAX_CUBEMAPS  4
#define MAX_VOLUMES      32
#define MAX_UINT_VOLUMES 2

#define UINT_SLOT_ALBEDO 0
#define UINT_SLOT_NORMAL 1

#define MAX_MATERIALS 64

#define VOLUME_SLOT_ALBEDO          0
#define VOLUME_SLOT_NORMAL          1
#define VOLUME_SLOT_RADIANCE        2
#define VOLUME_SLOT_RADIANCE_SMOOTH 3
#define VOLUME_SLOT_CASCADE         4
#define VOLUME_SLOT_IRRADIANCE      10
#define VOLUME_SLOT_SKY_OCCLUSION         16
#define VOLUME_SLOT_SKY_OCCLUSION_SCRATCH 17
#define VOLUME_SLOT_SCREEN_GI             18
#define VOLUME_SLOT_HANDOFF               24

#define LIGHT_DIRECTIONS 6
#define LIGHT_GI_STRENGTH  1.0
#define LIGHT_OCCLUSION_STRENGTH 1.0
#define LIGHT_SKY_STRENGTH 1.0
#define LIGHT_BOUNCE_STRENGTH 0.95
#define LIGHT_BLEND           0.25

#define RC_CASCADE_COUNT  6
#define RC_PROBE_SIZE     64
#define RC_DIR_RES        4
#define RC_TILE_SIZE      (RC_PROBE_SIZE * RC_DIR_RES)
#define RC_BASE_STEPS     4
#define RC_GROUP_SIZE     8
#define RC_INTERVAL_SCALE 2.0f
#define RC_EXTINCTION     24.0f
#define RC_INV_PI         0.31830989
#define RC_PROBE_SOLID    0.5
#define RC_SUN_STEPS      96
#define RC_SUN_FRINGE     0.3

#define RC_SCREEN_TILE    4
#define RC_SCREEN_HANDOFF 2
#define RC_SCREEN_DIR_RES (RC_DIR_RES << (RC_SCREEN_HANDOFF - 1))
#define RC_IRRADIANCE_SIZE (RC_PROBE_SIZE >> RC_SCREEN_HANDOFF)
#define RC_SCREEN_GROUP 8
#define RC_SCREEN_NORMAL_TAP 2
#define RC_SCREEN_MAX_X 960
#define RC_SCREEN_MAX_Y 540

#define VOLUME_GROUP_SIZE 4
#define VOXEL_GROUP_SIZE  64

#define VOLUME_GRID_SIZE    128
#define LIGHT_GRID_SIZE     128
#define VOLUME_WORLD_EXTENT 6.0f

#define VOLUME_MODE_SLICE  0
#define VOLUME_MODE_COLUMN 1
#define VOLUME_MODE_CAMERA 2
#define VOLUME_MODE_LIGHT  3

#define VOLUME_MARCH_STEPS 512

#define TEXTURE_NONE 0xFFFFFFFF

#define TEXTURE_SLOT_SCENE MAX_TEXTURES
#define TEXTURE_SLOT_POST  (TEXTURE_SLOT_SCENE + 1)
#define TEXTURE_SLOT_DEPTH (TEXTURE_SLOT_POST + 1)
#define TEXTURE_HEAP_SIZE  (TEXTURE_SLOT_DEPTH + 1)

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

struct rc_inject_params
{
    uint SolidSlot;
    uint NormalSlot;
    uint RadianceSlot;
    uint IrradianceSlot;

    uint LightSize;
    uint SkySlot;
    uint InjectPad1;
    uint InjectPad2;
};

struct rc_trace_params
{
    uint RadianceSlot;
    uint CascadeSlot;
    uint ProbeSize;
    uint DirRes;

    uint  Steps;
    uint  TracePad0;
    float IntervalStart;
    float IntervalLength;
};

struct rc_merge_params
{
    uint ParentSlot;
    uint ChildSlot;
    uint ParentProbeSize;
    uint ParentDirRes;

    uint ChildProbeSize;
    uint RadianceSlot;
    uint MergePad1;
    uint MergePad2;
};

struct rc_resolve_params
{
    uint CascadeSlot;
    uint IrradianceSlot;
    uint ProbeSize;
    uint DirRes;

    uint RadianceSlot;
    uint ResolvePad0;
    uint ResolvePad1;
    uint ResolvePad2;
};

struct rc_screen_params
{
    uint RadianceSlot;
    uint CascadeSlot;
    uint ScreenSlot;
    uint DepthSlot;

    uint  ProbeSize;
    uint  DirRes;
    uint  Steps;
    float IntervalLength;

    uint ProbeCountX;
    uint ProbeCountY;
    uint ScreenPad0;
    uint ScreenPad1;
};


#ifndef __cplusplus
    typedef vk::BufferPointer<frame_globals, 16>     frame_globals_ptr;
    typedef vk::BufferPointer<draw_params, 16>       draw_params_ptr;
    typedef vk::BufferPointer<image_params, 16>      image_params_ptr;
    typedef vk::BufferPointer<volume_params, 16>     volume_params_ptr;
    typedef vk::BufferPointer<voxelize_params, 16>   voxelize_params_ptr;
    typedef vk::BufferPointer<downsample_params, 16> downsample_params_ptr;
    typedef vk::BufferPointer<rc_inject_params, 16>  rc_inject_params_ptr;
    typedef vk::BufferPointer<rc_trace_params, 16>   rc_trace_params_ptr;
    typedef vk::BufferPointer<rc_merge_params, 16>   rc_merge_params_ptr;
    typedef vk::BufferPointer<rc_resolve_params, 16> rc_resolve_params_ptr;
    typedef vk::BufferPointer<rc_screen_params, 16>  rc_screen_params_ptr;
    typedef vk::BufferPointer<skybox_params, 16>     skybox_params_ptr;
    typedef vk::BufferPointer<rect_params, 16>       rect_params_ptr;

    static const float3 LightAxis[LIGHT_DIRECTIONS] =
    {
        float3( 1.0,  0.0,  0.0),
        float3(-1.0,  0.0,  0.0),
        float3( 0.0,  1.0,  0.0),
        float3( 0.0, -1.0,  0.0),
        float3( 0.0,  0.0,  1.0),
        float3( 0.0,  0.0, -1.0),
    };

    float3 LocalToUVW(float3 local)
    {
        return (local + VOLUME_WORLD_EXTENT) / (2.0 * VOLUME_WORLD_EXTENT);
    }

    float3 SmoothUVW(float3 uvw, float size)
    {
        float3 coord = uvw * size - 0.5;
        float3 anchor = floor(coord);
        float3 fraction = coord - anchor;

        fraction = fraction * fraction * (3.0 - 2.0 * fraction);

        return (anchor + fraction + 0.5) / size;
    }

    float3 OctDecode(float2 f)
    {
        float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));

        float t = saturate(-n.z);

        n.x += n.x >= 0.0 ? -t : t;
        n.y += n.y >= 0.0 ? -t : t;

        return normalize(n);
    }

    float3 RcDirection(uint2 dirUV, uint dirRes)
    {
        float2 f = ((float2)dirUV + 0.5) / (float)dirRes * 2.0 - 1.0;

        return OctDecode(f);
    }

    float3 RcProbeLocal(uint3 probe, uint probeSize)
    {
        return (((float3)probe + 0.5) / (float)probeSize - 0.5) * (2.0 * VOLUME_WORLD_EXTENT);
    }

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

    rc_inject_params LoadRcInjectParams(uint64_t address)
    {
        return rc_inject_params_ptr(address).Get();
    }

    rc_trace_params LoadRcTraceParams(uint64_t address)
    {
        return rc_trace_params_ptr(address).Get();
    }

    rc_merge_params LoadRcMergeParams(uint64_t address)
    {
        return rc_merge_params_ptr(address).Get();
    }

    rc_resolve_params LoadRcResolveParams(uint64_t address)
    {
        return rc_resolve_params_ptr(address).Get();
    }

    rc_screen_params LoadRcScreenParams(uint64_t address)
    {
        return rc_screen_params_ptr(address).Get();
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
