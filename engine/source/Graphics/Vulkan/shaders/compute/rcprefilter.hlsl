#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    downsample_params params = LoadDownsampleParams(pc.ParamsPtr);

    uint tile = params.VoxelSize * params.LightNormalSlot;

    if (id.x >= tile || id.y >= tile || id.z >= params.VoxelSize)
    {
        return;
    }

    uint2 probeXY = id.xy / params.LightNormalSlot;
    uint2 dirUV   = id.xy % params.LightNormalSlot;

    uint ratio = params.SolidSlot / params.LightNormalSlot;

    float4 total = float4(0.0, 0.0, 0.0, 0.0);

    for (uint v = 0; v < ratio; ++v)
    {
        for (uint u = 0; u < ratio; ++u)
        {
            uint2 src = probeXY * params.SolidSlot + dirUV * ratio + uint2(u, v);

            total += VolumesRW[params.AlbedoSlot][uint3(src, id.z)];
        }
    }

    VolumesRW[params.NormalSlot][id] = total / (float)(ratio * ratio);
}
