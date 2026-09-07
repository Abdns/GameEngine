#include "Vulkan.h"

internal void PushVoxelVolumes(render_commands *commands)
{
    PushLoadVolume(commands, VOLUME_SLOT_ALBEDO,                VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VolumeFormat_RGBA16F);
    PushLoadVolume(commands, VOLUME_SLOT_NORMAL,                VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VolumeFormat_RGBA16F);
    PushLoadVolume(commands, VOLUME_SLOT_SKY_OCCLUSION,         VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VolumeFormat_RGBA16F);
    PushLoadVolume(commands, VOLUME_SLOT_SKY_OCCLUSION_SCRATCH, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VolumeFormat_RGBA16F);

    PushLoadVolume(commands, VOLUME_SLOT_RADIANCE,        LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, VolumeFormat_RGBA16F);
    PushLoadVolume(commands, VOLUME_SLOT_RADIANCE_SMOOTH, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, VolumeFormat_RGBA16F);

    for (uint32 i = RC_SCREEN_HANDOFF; i < RC_CASCADE_COUNT; ++i)
    {
        PushLoadVolume(commands, VOLUME_SLOT_CASCADE + i, RC_TILE_SIZE, RC_TILE_SIZE, RC_PROBE_SIZE >> i, VolumeFormat_RGBA16F);
    }

    for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
    {
        PushLoadVolume(commands, VOLUME_SLOT_IRRADIANCE + i, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE, VolumeFormat_RGBA16F);
    }

    for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
    {
        PushLoadVolume(commands, VOLUME_SLOT_SCREEN_GI + i, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1, VolumeFormat_RGBA16F);
    }

    PushLoadVolume(commands, VOLUME_SLOT_SCREEN_META, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1, VolumeFormat_RGBA16F);

    uint32 handoffProbes = RC_PROBE_SIZE >> (RC_SCREEN_HANDOFF + 1);
    uint32 handoffTile   = handoffProbes * RC_SCREEN_DIR_RES;

    PushLoadVolume(commands, VOLUME_SLOT_HANDOFF, handoffTile, handoffTile, handoffProbes, VolumeFormat_RGBA16F);

    for (uint32 i = 0; i < MAX_UINT_VOLUMES; ++i)
    {
        PushLoadVolume(commands, i, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VolumeFormat_R32U);
    }
}
