#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D Volumes[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    downsample_params params = LoadDownsampleParams(pc.ParamsPtr);

    if (any(id >= params.VoxelSize))
    {
        return;
    }

    float inv = 1.0 / (float)params.VoxelSize;

    float3 uvw = ((float3)id + 0.5) * inv;

    float4 total = float4(0.0, 0.0, 0.0, 0.0);

    for (uint corner = 0; corner < 8; ++corner)
    {
        float3 offset = float3(
            (corner & 1) ? 0.5 : -0.5,
            (corner & 2) ? 0.5 : -0.5,
            (corner & 4) ? 0.5 : -0.5) * inv;

        total += Volumes[params.AlbedoSlot].SampleLevel(VolumeSamp, uvw + offset, 0);
    }

    VolumesRW[params.NormalSlot][id] = total * 0.125;
}
