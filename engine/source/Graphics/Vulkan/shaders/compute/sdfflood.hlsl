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

    int3 coord = int3(id);
    int  size  = (int)params.GridSize;
    int  step  = (int)params.StepSize;

    float4 best     = VolumesRW[params.SourceSlot][id];
    float  bestDist = best.w > 0.0 ? distance((float3)coord, best.xyz) : 1e30;

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                int3 neighbour = coord + int3(x, y, z) * step;

                if (any(neighbour < 0) || any(neighbour >= size))
                {
                    continue;
                }

                float4 candidate = VolumesRW[params.SourceSlot][uint3(neighbour)];

                if (candidate.w <= 0.0)
                {
                    continue;
                }

                float candidateDist = distance((float3)coord, candidate.xyz);

                if (candidateDist < bestDist)
                {
                    bestDist = candidateDist;
                    best     = candidate;
                }
            }
        }
    }

    VolumesRW[params.TargetSlot][id] = best;
}
