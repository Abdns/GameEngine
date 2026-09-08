#ifndef GIINTEROP_H
#define GIINTEROP_H

// Scene scale and sampling quality (world distances use engine units).
#define LIGHT_DIRECTIONS 6
#define LIGHT_GI_STRENGTH  1.0
#define LIGHT_SKY_STRENGTH 1.0
#define LIGHT_BOUNCE_STRENGTH 0.95
// Temporal response in seconds. Occupancy is always from the current frame.
#define GI_HISTORY_SECONDS 0.08f
#define GI_SURFACE_SEARCH_VOXELS 2.05f
#define GI_ENV_SIZE 64u
#define GI_ENV_SPECULAR_LEVELS 7u
#define GI_ENV_LAYERS (GI_ENV_SPECULAR_LEVELS + 1u)
#define GI_ENV_SAMPLES 128u

#define RC_CASCADE_COUNT  6
#define RC_PROBE_SIZE     64u
#define RC_DIR_RES        4u
#define RC_TILE_SIZE      (RC_PROBE_SIZE * RC_DIR_RES)
#define RC_GROUP_SIZE     8
#define RC_INTERVAL_SCALE 2.0f
#define RC_INV_PI         0.31830989

#define RC_SCREEN_TILE    4
#define RC_SCREEN_HANDOFF 2
#define RC_SCREEN_DIR_RES (RC_DIR_RES << (RC_SCREEN_HANDOFF - 1))
#define RC_IRRADIANCE_SIZE (RC_PROBE_SIZE >> RC_SCREEN_HANDOFF)
#define RC_SCREEN_GROUP 8
#define RC_SCREEN_NORMAL_TAP 2
#define RC_SCREEN_MAX_X 960u
#define RC_SCREEN_MAX_Y 540u

#define RC_CASCADE_PROBE_SIZE(cascade) (RC_PROBE_SIZE >> (cascade))
#define RC_CASCADE_DIR_RES(cascade)    (RC_DIR_RES << (cascade))

#define RC_HANDOFF_CASCADE    (RC_SCREEN_HANDOFF + 1)
#define RC_HANDOFF_PROBE_SIZE RC_CASCADE_PROBE_SIZE(RC_HANDOFF_CASCADE)
#define RC_HANDOFF_TILE_SIZE  (RC_HANDOFF_PROBE_SIZE * RC_SCREEN_DIR_RES)

#define RC_SCREEN_TILES(pixels)    (((pixels) + RC_SCREEN_TILE - 1) / RC_SCREEN_TILE)
#define RC_SCREEN_PROBES_X(width)  (RC_SCREEN_TILES(width)  > RC_SCREEN_MAX_X ? RC_SCREEN_MAX_X : RC_SCREEN_TILES(width))
#define RC_SCREEN_PROBES_Y(height) (RC_SCREEN_TILES(height) > RC_SCREEN_MAX_Y ? RC_SCREEN_MAX_Y : RC_SCREEN_TILES(height))

#define UINT_SLOT_ALBEDO 0
#define UINT_SLOT_NORMAL 1
#define UINT_SLOT_COUNT  2

#define VOLUME_SLOT_ALBEDO                0
#define VOLUME_SLOT_NORMAL                1
#define VOLUME_SLOT_RADIANCE              2
#define VOLUME_SLOT_RADIANCE_SMOOTH       3
#define VOLUME_SLOT_CASCADE               4
#define VOLUME_SLOT_IRRADIANCE            (VOLUME_SLOT_CASCADE     + RC_CASCADE_COUNT)
#define VOLUME_SLOT_SCREEN_GI             (VOLUME_SLOT_IRRADIANCE  + LIGHT_DIRECTIONS)
#define VOLUME_SLOT_HANDOFF               (VOLUME_SLOT_SCREEN_GI   + 1)
#define VOLUME_SLOT_SCREEN_META           (VOLUME_SLOT_HANDOFF     + 1)
#define VOLUME_SLOT_ENVIRONMENT           (VOLUME_SLOT_SCREEN_META + 1)
#define VOLUME_SLOT_COUNT                 (VOLUME_SLOT_ENVIRONMENT + 1)

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

#define GI_DEBUG_FINAL             0u
#define GI_DEBUG_DIRECT            1u
#define GI_DEBUG_INDIRECT          2u
#define GI_DEBUG_SKY_VISIBILITY    3u
#define GI_DEBUG_SCREEN_CONFIDENCE 4u
#define GI_DEBUG_HISTORY_REJECTION 5u
#define GI_DEBUG_REFLECTIONS       6u
#define GI_DEBUG_COUNT             7u

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
    float4 Tint;

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
// Buffer contracts:
// Albedo: diffuse reflectance RGB, CURRENT binary occupancy A.
// Normal: encoded averaged vertex normal RGB, geometry/material history validity A.
// Radiance/Smooth: outgoing diffuse radiance RGB, CURRENT occupancy A.
// Cascade/Handoff: integrated segment radiance RGB, transmittance A.
// Irradiance/Screen: diffuse E/pi RGB, cosine-weighted sky visibility A.
// ScreenMeta: world normal RGB, positive view depth A (zero = no surface).
// Environment: diffuse E/pi layer 0; GGX radiance layers 1..7.
#define GiAlbedo              Volumes[VOLUME_SLOT_ALBEDO]
#define GiAlbedoRW            VolumesRW[VOLUME_SLOT_ALBEDO]
#define GiNormal              Volumes[VOLUME_SLOT_NORMAL]
#define GiNormalRW            VolumesRW[VOLUME_SLOT_NORMAL]
#define GiRadiance            Volumes[VOLUME_SLOT_RADIANCE]
#define GiRadianceRW          VolumesRW[VOLUME_SLOT_RADIANCE]
#define GiRadianceSmooth      Volumes[VOLUME_SLOT_RADIANCE_SMOOTH]
#define GiRadianceSmoothRW    VolumesRW[VOLUME_SLOT_RADIANCE_SMOOTH]
#define GiCascade(Index)      Volumes[VOLUME_SLOT_CASCADE + (Index)]
#define GiCascadeRW(Index)    VolumesRW[VOLUME_SLOT_CASCADE + (Index)]
#define GiIrradiance(Index)   Volumes[VOLUME_SLOT_IRRADIANCE + (Index)]
#define GiIrradianceRW(Index) VolumesRW[VOLUME_SLOT_IRRADIANCE + (Index)]
#define GiScreenLight         Volumes[VOLUME_SLOT_SCREEN_GI]
#define GiScreenLightRW       VolumesRW[VOLUME_SLOT_SCREEN_GI]
#define GiHandoff             Volumes[VOLUME_SLOT_HANDOFF]
#define GiHandoffRW           VolumesRW[VOLUME_SLOT_HANDOFF]
#define GiScreenMeta          Volumes[VOLUME_SLOT_SCREEN_META]
#define GiScreenMetaRW        VolumesRW[VOLUME_SLOT_SCREEN_META]
#define GiEnvironment         Volumes[VOLUME_SLOT_ENVIRONMENT]
#define GiEnvironmentRW       VolumesRW[VOLUME_SLOT_ENVIRONMENT]
#define GiAlbedoUintRW        UintVolumesRW[UINT_SLOT_ALBEDO]
#define GiNormalUintRW        UintVolumesRW[UINT_SLOT_NORMAL]
#endif

#endif
