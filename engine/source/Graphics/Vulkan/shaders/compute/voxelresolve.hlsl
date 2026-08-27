#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::image_format("r32ui")]]
[[vk::binding(BINDING_UINT_VOLUMES, SET_GLOBAL)]] RWTexture3D<uint> UintVolumesRW[MAX_UINT_VOLUMES];

[[vk::push_constant]] push_constants pc;

float3 UnpackColorKey(uint key)
{
    uint3 quantized = uint3((key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF);

    return (float3)quantized / 255.0;
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    downsample_params params = LoadDownsampleParams(pc.ParamsPtr);

    if (any(id >= params.VoxelSize))
    {
        return;
    }

    uint albedoKey = UintVolumesRW[UINT_SLOT_ALBEDO][id];
    uint normalKey = UintVolumesRW[UINT_SLOT_NORMAL][id];

    float occupancy = albedoKey ? 1.0 : 0.0;

    VolumesRW[params.AlbedoSlot][id] = float4(UnpackColorKey(albedoKey), occupancy);
    VolumesRW[params.NormalSlot][id] = float4(UnpackColorKey(normalKey), occupancy);
}
