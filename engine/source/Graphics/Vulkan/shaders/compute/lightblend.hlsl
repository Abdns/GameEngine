#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    propagate_params params = LoadPropagateParams(pc.ParamsPtr);

    if (any(id >= params.LightSize))
    {
        return;
    }

    for (uint direction = 0; direction < LIGHT_DIRECTIONS; ++direction)
    {
        float4 current = VolumesRW[params.SourceSlot + direction][id];
        float4 history = VolumesRW[params.TargetSlot + direction][id];

        VolumesRW[params.TargetSlot + direction][id] = lerp(history, current, LIGHT_BLEND);
    }
}
