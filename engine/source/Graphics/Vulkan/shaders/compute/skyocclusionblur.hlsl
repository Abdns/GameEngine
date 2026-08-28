#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    downsample_params params = LoadDownsampleParams(pc.ParamsPtr);

    if (any(id >= params.VoxelSize))
    {
        return;
    }

    int size = (int)params.VoxelSize;

    float total  = 0.0;
    float weight = 0.0;

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                int3 coord = clamp(int3(id) + int3(x, y, z), 0, size - 1);

                float tap = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0) * (z == 0 ? 2.0 : 1.0);

                total  += VolumesRW[params.AlbedoSlot][uint3(coord)].r * tap;
                weight += tap;
            }
        }
    }

    VolumesRW[params.NormalSlot][id] = float4(total / weight, 0.0, 0.0, 1.0);
}
