#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D Volumes[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    rc_merge_params params = LoadRcMergeParams(pc.ParamsPtr);

    if (id.x >= RC_TILE_SIZE || id.y >= RC_TILE_SIZE || id.z >= params.ParentProbeSize)
    {
        return;
    }

    float4 nearField = VolumesRW[params.ParentSlot][id];

    if (nearField.a <= 0.0)
    {
        return;
    }

    uint2 probeXY = id.xy / params.ParentDirRes;
    uint2 dirUV   = id.xy % params.ParentDirRes;

    float3 grid = (float3(probeXY, id.z) + 0.5) * 0.5 - 0.5;

    int3   base     = (int3)floor(grid);
    float3 fraction = grid - (float3)base;

    fraction = fraction * fraction * (3.0 - 2.0 * fraction);

    uint childDirRes = params.ParentDirRes * 2;

    uint3 childCorner[8];
    float childWeight[8];
    float childTotal = 0.0;

    for (uint c = 0; c < 8; ++c)
    {
        int3 offset = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);

        float3 axisWeight = lerp(1.0 - fraction, fraction, (float3)offset);

        int3 probe = clamp(base + offset, int3(0, 0, 0), (int3)(params.ChildProbeSize - 1));

        float occupancy = Volumes[params.RadianceSlot].SampleLevel(VolumeSamp, LocalToUVW(RcProbeLocal((uint3)probe, params.ChildProbeSize)), 0).a;

        childCorner[c] = (uint3)probe;
        childWeight[c] = axisWeight.x * axisWeight.y * axisWeight.z * saturate(1.0 - occupancy);
        childTotal    += childWeight[c];
    }

    if (childTotal < 1e-4)
    {
        childTotal = 0.0;

        for (uint fallback = 0; fallback < 8; ++fallback)
        {
            int3 offset = int3(fallback & 1, (fallback >> 1) & 1, (fallback >> 2) & 1);

            float3 axisWeight = lerp(1.0 - fraction, fraction, (float3)offset);

            childWeight[fallback] = axisWeight.x * axisWeight.y * axisWeight.z;
            childTotal           += childWeight[fallback];
        }
    }

    float childNorm = 1.0 / max(childTotal, 1e-4);

    float4 farField = float4(0.0, 0.0, 0.0, 0.0);

    for (uint dv = 0; dv < 2; ++dv)
    {
        for (uint du = 0; du < 2; ++du)
        {
            uint2 childDir = dirUV * 2 + uint2(du, dv);

            for (uint tap = 0; tap < 8; ++tap)
            {
                uint3 coord = uint3(childCorner[tap].xy * childDirRes + childDir, childCorner[tap].z);

                farField += VolumesRW[params.ChildSlot][coord] * childWeight[tap];
            }
        }
    }

    farField *= childNorm * 0.25;

    VolumesRW[params.ParentSlot][id] = float4(nearField.rgb + nearField.a * farField.rgb, nearField.a * farField.a);
}
