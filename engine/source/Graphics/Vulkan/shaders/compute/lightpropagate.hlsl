#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

static const float LIGHT_FORWARD = 0.55;
static const float LIGHT_SIDE    = 0.08;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    propagate_params params = LoadPropagateParams(pc.ParamsPtr);

    if (any(id >= params.LightSize))
    {
        return;
    }

    int size = (int)params.LightSize;

    float transmit = saturate(1.0 - VolumesRW[params.SolidSlot][id].a);

    for (uint direction = 0; direction < LIGHT_DIRECTIONS; ++direction)
    {
        int3 upstream = int3(id) - int3(LightAxis[direction]);

        float3 result = float3(0.0, 0.0, 0.0);

        if (all(upstream >= 0) && all(upstream < size))
        {
            uint3 source = uint3(upstream);

            float3 forward = VolumesRW[params.SourceSlot + direction][source].rgb;

            float3 sideways = float3(0.0, 0.0, 0.0);

            for (uint other = 0; other < LIGHT_DIRECTIONS; ++other)
            {
                if (other == direction || other == (direction ^ 1))
                {
                    continue;
                }

                sideways += VolumesRW[params.SourceSlot + other][source].rgb;
            }

            result = transmit * (forward * LIGHT_FORWARD + sideways * LIGHT_SIDE);
        }

        VolumesRW[params.TargetSlot + direction][id] = float4(result, 0.0);

        float4 accumulated = VolumesRW[params.SumSlot + direction][id];

        VolumesRW[params.SumSlot + direction][id] = accumulated + float4(result, 0.0);
    }
}
