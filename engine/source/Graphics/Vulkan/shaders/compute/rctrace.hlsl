#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D Volumes[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    rc_trace_params params = LoadRcTraceParams(pc.ParamsPtr);

    if (id.x >= RC_TILE_SIZE || id.y >= RC_TILE_SIZE || id.z >= params.ProbeSize)
    {
        return;
    }

    uint2 probeXY = id.xy / params.DirRes;
    uint2 dirUV   = id.xy % params.DirRes;

    float3 origin    = RcProbeLocal(uint3(probeXY, id.z), params.ProbeSize);
    float3 direction = RcDirection(dirUV, params.DirRes);

    float stepSize = params.IntervalLength / (float)params.Steps;

    float3 radiance      = float3(0.0, 0.0, 0.0);
    float  transmittance = 1.0;

    for (uint i = 0; i < params.Steps; ++i)
    {
        float travel = params.IntervalStart + stepSize * ((float)i + 0.5);

        float3 uvw = LocalToUVW(origin + direction * travel);

        if (any(uvw < 0.0) || any(uvw > 1.0))
        {
            break;
        }

        float4 voxel = Volumes[params.RadianceSlot].SampleLevel(VolumeSamp, SmoothUVW(uvw, (float)LIGHT_GRID_SIZE), 0);

        if (voxel.a > 0.001)
        {
            float absorbed = 1.0 - exp(-voxel.a * RC_EXTINCTION * stepSize);

            radiance      += transmittance * (voxel.rgb / voxel.a) * absorbed;
            transmittance *= 1.0 - absorbed;

            if (transmittance < 0.005)
            {
                transmittance = 0.0;
                break;
            }
        }
    }

    VolumesRW[params.CascadeSlot][id] = float4(radiance, transmittance);
}
