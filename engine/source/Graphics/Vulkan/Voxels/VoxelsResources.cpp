#include "Vulkan.h"

internal gpu_image *CreateVolume(vulkan_context *context, vulkan_resources *res, uint32 VolumeSlot, uint32 Width, uint32 Height, uint32 Depth, VkCommandBuffer cmd)
{
    Assert(VolumeSlot < MAX_VOLUMES);

    gpu_image *volume = &res->Volumes[VolumeSlot];
    Assert(volume->Image == VK_NULL_HANDLE);

    *volume = CreateImage(context, Image_Volume, VK_FORMAT_R16G16B16A16_SFLOAT, Width, Height, Depth, 1);

    WriteImageDescriptor(context, &res->Heap, res->Heap.VolumeOffset, VolumeSlot, volume->View, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    WriteImageDescriptor(context, &res->Heap, res->Heap.StorageVolumeOffset, VolumeSlot, volume->View, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);

    return volume;
}

internal gpu_image *CreateUintVolume(vulkan_context *context, vulkan_resources *res, uint32 slot, uint32 size, VkCommandBuffer cmd)
{
    Assert(slot < MAX_UINT_VOLUMES);

    gpu_image *volume = &res->UintVolumes[slot];
    Assert(volume->Image == VK_NULL_HANDLE);

    *volume = CreateImage(context, Image_Volume, VK_FORMAT_R32_UINT, size, size, size, 1);

    WriteImageDescriptor(context, &res->Heap, res->Heap.UintVolumeOffset, slot, volume->View, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);

    return volume;
}

internal void CreateVoxelVolumes(vulkan_context *context, vulkan_resources *res, VkCommandBuffer cmd)
{
    CreateVolume(context, res, VOLUME_SLOT_ALBEDO,                VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, cmd);
    CreateVolume(context, res, VOLUME_SLOT_NORMAL,                VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, cmd);
    CreateVolume(context, res, VOLUME_SLOT_SKY_OCCLUSION,         VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, cmd);
    CreateVolume(context, res, VOLUME_SLOT_SKY_OCCLUSION_SCRATCH, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, cmd);

    CreateVolume(context, res, VOLUME_SLOT_RADIANCE,        LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, cmd);
    CreateVolume(context, res, VOLUME_SLOT_RADIANCE_SMOOTH, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, cmd);

    for (uint32 i = RC_SCREEN_HANDOFF; i < RC_CASCADE_COUNT; ++i)
    {
        CreateVolume(context, res, VOLUME_SLOT_CASCADE + i, RC_TILE_SIZE, RC_TILE_SIZE, RC_PROBE_SIZE >> i, cmd);
    }

    for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
    {
        CreateVolume(context, res, VOLUME_SLOT_IRRADIANCE + i, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE, cmd);
    }

    for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
    {
        CreateVolume(context, res, VOLUME_SLOT_SCREEN_GI + i, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1, cmd);
    }

    CreateVolume(context, res, VOLUME_SLOT_SCREEN_META, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1, cmd);

    uint32 handoffProbes = RC_PROBE_SIZE >> (RC_SCREEN_HANDOFF + 1);
    uint32 handoffTile   = handoffProbes * RC_SCREEN_DIR_RES;

    CreateVolume(context, res, VOLUME_SLOT_HANDOFF, handoffTile, handoffTile, handoffProbes, cmd);

    for (uint32 i = 0; i < MAX_UINT_VOLUMES; ++i)
    {
        CreateUintVolume(context, res, i, VOLUME_GRID_SIZE, cmd);
    }
}
