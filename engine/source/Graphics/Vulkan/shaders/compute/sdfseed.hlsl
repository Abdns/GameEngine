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

    float occupancy = VolumesRW[params.SourceSlot][id].a;

    float4 seed = float4(0.0, 0.0, 0.0, 0.0);

    if (occupancy > 0.0)
    {
        seed = float4((float3)id, 1.0);
    }

    VolumesRW[params.TargetSlot][id] = seed;
}
