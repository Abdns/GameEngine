#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    downsample_params params = LoadDownsampleParams(pc.ParamsPtr);

    if (any(id >= params.LightSize))
    {
        return;
    }

    uint  ratio = params.VoxelSize / params.LightSize;
    uint3 base  = id * ratio;

    float3 albedo = float3(0.0, 0.0, 0.0);
    float3 normal = float3(0.0, 0.0, 0.0);
    float  filled = 0.0;

    for (uint z = 0; z < ratio; ++z)
    {
        for (uint y = 0; y < ratio; ++y)
        {
            for (uint x = 0; x < ratio; ++x)
            {
                uint3 coord = base + uint3(x, y, z);

                float4 voxel = VolumesRW[params.AlbedoSlot][coord];

                if (voxel.a > 0.0)
                {
                    albedo += voxel.rgb;
                    normal += VolumesRW[params.NormalSlot][coord].rgb * 2.0 - 1.0;
                    filled += 1.0;
                }
            }
        }
    }

    float4 solid       = float4(0.0, 0.0, 0.0, 0.0);
    float4 lightNormal = float4(0.5, 0.5, 0.5, 0.0);

    if (filled > 0.0)
    {
        float coverage = filled / (float)(ratio * ratio * ratio);

        solid = float4(albedo / filled, coverage);

        float length2 = dot(normal, normal);

        if (length2 > 1e-6)
        {
            lightNormal = float4(normal * rsqrt(length2) * 0.5 + 0.5, 1.0);
        }
    }

    VolumesRW[params.SolidSlot][id]       = solid;
    VolumesRW[params.LightNormalSlot][id] = lightNormal;
}
