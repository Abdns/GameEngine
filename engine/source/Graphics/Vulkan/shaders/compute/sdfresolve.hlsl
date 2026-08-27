#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    flood_params params = LoadFloodParams(pc.ParamsPtr);

    if (any(id >= params.GridSize))
    {
        return;
    }

    float4 seed = VolumesRW[params.SourceSlot][id];

    float voxelSize = (2.0 * VOXEL_WORLD_EXTENT) / (float)params.GridSize;

    float result = (float)params.GridSize * voxelSize;

    if (seed.w > 0.0)
    {
        result = distance((float3)id, seed.xyz) * voxelSize;
    }

    VolumesRW[params.TargetSlot][id] = float4(result, 0.0, 0.0, 1.0);
}
