#include "Vulkan.h"

internal gpu_volume *CreateVolume(vulkan_context *context, vulkan_resources *res, uint32 VolumeSlot, uint32 Width, uint32 Height, uint32 Depth, VkFormat Format)
{
    Assert(VolumeSlot < MAX_VOLUMES);

    gpu_volume *volume = &res->Volumes[VolumeSlot];
    Assert(volume->Image == VK_NULL_HANDLE);

    volume->Width  = Width;
    volume->Height = Height;
    volume->Depth  = Depth;
    volume->Image  = CreateVolumeImage(context, Width, Height, Depth, Format, &volume->Memory);
    volume->View   = CreateVolumeImageView(context->device, volume->Image, Format);

    WriteImageDescriptor(context, &res->Heap, res->Heap.VolumeOffset, VolumeSlot, volume->View, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    WriteImageDescriptor(context, &res->Heap, res->Heap.StorageVolumeOffset, VolumeSlot, volume->View, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    return volume;
}

internal gpu_volume *CreateUintVolume(vulkan_context *context, vulkan_resources *res, uint32 slot, uint32 size)
{
    Assert(slot < MAX_UINT_VOLUMES);

    gpu_volume *volume = &res->UintVolumes[slot];
    Assert(volume->Image == VK_NULL_HANDLE);

    volume->Width  = size;
    volume->Height = size;
    volume->Depth  = size;
    volume->Image  = CreateVolumeImage(context, size, size, size, VK_FORMAT_R32_UINT, &volume->Memory);
    volume->View   = CreateVolumeImageView(context->device, volume->Image, VK_FORMAT_R32_UINT);

    WriteImageDescriptor(context, &res->Heap, res->Heap.UintVolumeOffset, slot, volume->View, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    return volume;
}

internal void CreateVoxelVolumes(vulkan_context *context, vulkan_resources *res, VkCommandBuffer cmd)
{
    uint32 voxelSlots[] = { VOLUME_SLOT_ALBEDO, VOLUME_SLOT_NORMAL, VOLUME_SLOT_SKY_OCCLUSION, VOLUME_SLOT_SKY_OCCLUSION_SCRATCH };
    uint32 lightSlots[] = { VOLUME_SLOT_RADIANCE, VOLUME_SLOT_RADIANCE_SMOOTH };

    for (uint32 i = 0; i < ArrayCount(voxelSlots); ++i)
    {
        gpu_volume *volume = CreateVolume(context, res, voxelSlots[i], VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VOLUME_GRID_SIZE, VK_FORMAT_R16G16B16A16_SFLOAT);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }

    for (uint32 i = 0; i < ArrayCount(lightSlots); ++i)
    {
        gpu_volume *volume = CreateVolume(context, res, lightSlots[i], LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, LIGHT_GRID_SIZE, VK_FORMAT_R16G16B16A16_SFLOAT);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }

    for (uint32 i = RC_SCREEN_HANDOFF; i < RC_CASCADE_COUNT; ++i)
    {
        gpu_volume *volume = CreateVolume(context, res, VOLUME_SLOT_CASCADE + i, RC_TILE_SIZE, RC_TILE_SIZE, RC_PROBE_SIZE >> i, VK_FORMAT_R16G16B16A16_SFLOAT);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }

    for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
    {
        gpu_volume *volume = CreateVolume(context, res, VOLUME_SLOT_IRRADIANCE + i, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE, RC_IRRADIANCE_SIZE, VK_FORMAT_R16G16B16A16_SFLOAT);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }

    for (uint32 i = 0; i < LIGHT_DIRECTIONS; ++i)
    {
        gpu_volume *volume = CreateVolume(context, res, VOLUME_SLOT_SCREEN_GI + i, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1, VK_FORMAT_R16G16B16A16_SFLOAT);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }

    {
        gpu_volume *volume = CreateVolume(context, res, VOLUME_SLOT_SCREEN_META, RC_SCREEN_MAX_X, RC_SCREEN_MAX_Y, 1, VK_FORMAT_R16G16B16A16_SFLOAT);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }

    {
        uint32 handoffProbes = RC_PROBE_SIZE >> (RC_SCREEN_HANDOFF + 1);
        uint32 handoffTile   = handoffProbes * RC_SCREEN_DIR_RES;

        gpu_volume *volume = CreateVolume(context, res, VOLUME_SLOT_HANDOFF, handoffTile, handoffTile, handoffProbes, VK_FORMAT_R16G16B16A16_SFLOAT);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }

    for (uint32 i = 0; i < MAX_UINT_VOLUMES; ++i)
    {
        gpu_volume *volume = CreateUintVolume(context, res, i, VOLUME_GRID_SIZE);
        CmdImageToGeneral(cmd, volume->Image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }
}
