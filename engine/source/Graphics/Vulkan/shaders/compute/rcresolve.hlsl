#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D Volumes[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

float ProbeOccupancy(uint slot, uint3 probe, uint probeSize)
{
    return Volumes[slot].SampleLevel(VolumeSamp, LocalToUVW(RcProbeLocal(probe, probeSize)), 0).a;
}

void AccumulateProbe(rc_resolve_params params, uint3 probe, float probeWeight, inout float3 total[LIGHT_DIRECTIONS], inout float skyTotal[LIGHT_DIRECTIONS], inout float weights[LIGHT_DIRECTIONS])
{
    for (uint v = 0; v < params.DirRes; ++v)
    {
        for (uint u = 0; u < params.DirRes; ++u)
        {
            float3 direction = RcDirection(uint2(u, v), params.DirRes);

            uint3 coord = uint3(probe.xy * params.DirRes + uint2(u, v), probe.z);

            float4 cascade = VolumesRW[params.CascadeSlot][coord];

            for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
            {
                float weight = max(dot(LightAxis[axis], direction), 0.0) * probeWeight;

                total[axis]    += cascade.rgb * weight;
                skyTotal[axis] += cascade.a * weight;
                weights[axis]  += weight;
            }
        }
    }
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    rc_resolve_params params = LoadRcResolveParams(pc.ParamsPtr);

    if (any(id >= params.ProbeSize))
    {
        return;
    }

    float3 total[LIGHT_DIRECTIONS];
    float  skyTotal[LIGHT_DIRECTIONS];
    float  weights[LIGHT_DIRECTIONS];

    for (uint clear = 0; clear < LIGHT_DIRECTIONS; ++clear)
    {
        total[clear]    = float3(0.0, 0.0, 0.0);
        skyTotal[clear] = 0.0;
        weights[clear]  = 0.0;
    }

    int size = (int)params.ProbeSize;

    float covered = 0.0;

    for (uint candidate = 0; candidate < LIGHT_DIRECTIONS + 1; ++candidate)
    {
        int3 offset = (candidate == 0) ? int3(0, 0, 0) : (int3)LightAxis[candidate - 1];

        int3 probe = int3(id) + offset;

        if (any(probe < 0) || any(probe >= size))
        {
            continue;
        }

        float free = saturate(1.0 - ProbeOccupancy(params.RadianceSlot, (uint3)probe, params.ProbeSize));

        float probeWeight = free * ((candidate == 0) ? 2.0 : 1.0);

        if (probeWeight <= 1e-3)
        {
            continue;
        }

        AccumulateProbe(params, (uint3)probe, probeWeight, total, skyTotal, weights);

        covered += probeWeight;
    }

    if (covered <= 1e-3)
    {
        AccumulateProbe(params, id, 1.0, total, skyTotal, weights);
    }

    for (uint store = 0; store < LIGHT_DIRECTIONS; ++store)
    {
        float norm = 1.0 / max(weights[store], 1e-4);

        VolumesRW[params.IrradianceSlot + store][id] = float4(total[store] * norm, saturate(skyTotal[store] * norm));
    }
}
