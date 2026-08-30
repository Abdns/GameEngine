#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(8, 1, 8)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    downsample_params params = LoadDownsampleParams(pc.ParamsPtr);

    if (id.x >= params.VoxelSize || id.z >= params.VoxelSize)
    {
        return;
    }

    float visibility = 1.0;

    for (int y = (int)params.VoxelSize - 1; y >= 0; --y)
    {
        uint3 coord = uint3(id.x, (uint)y, id.z);

        VolumesRW[params.NormalSlot][coord] = float4(visibility, 0.0, 0.0, 1.0);

        visibility *= saturate(1.0 - VolumesRW[params.AlbedoSlot][coord].a);
    }
}
