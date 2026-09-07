#include "Compute.hlsl"

void AccumulateProbe(uint cascadeSlot, uint dirRes, uint3 probe, float probeWeight, inout axis_light light)
{
    for (uint v = 0; v < dirRes; ++v)
    {
        for (uint u = 0; u < dirRes; ++u)
        {
            float3 direction = RcDirection(uint2(u, v), dirRes);

            uint3 coord = uint3(probe.xy * dirRes + uint2(u, v), probe.z);

            float4 cascade = VolumesRW[cascadeSlot][coord];

            AxisAccumulate(light, direction, cascade.rgb, cascade.a, probeWeight);
        }
    }
}

float ProbeOccupancy(uint3 probe, uint probeSize)
{
    return GiRadianceSmooth.SampleLevel(VolumeSamp, LocalToUVW(RcProbeLocal(probe, probeSize)), 0).a;
}

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void Trace(uint3 id : SV_DispatchThreadID)
{
    rc_cascade_params params = LoadPassParams(rc_cascade_params);

    uint cascade   = params.Cascade;
    uint probeSize = RC_CASCADE_PROBE_SIZE(cascade);
    uint dirRes    = RC_CASCADE_DIR_RES(cascade);

    if (id.x >= RC_TILE_SIZE || id.y >= RC_TILE_SIZE || id.z >= probeSize)
    {
        return;
    }

    uint2 probeXY = id.xy / dirRes;
    uint2 dirUV   = id.xy % dirRes;

    float3 origin    = RcProbeLocal(uint3(probeXY, id.z), probeSize);
    float3 direction = RcDirection(dirUV, dirRes);

    float start = 0.0;
    float span  = 0.0;

    RcCascadeInterval(cascade, start, span);

    GiCascadeRW(cascade)[id] = TraceVolume(VOLUME_SLOT_RADIANCE_SMOOTH, origin, direction, start, span, RC_CASCADE_STEPS(cascade));
}

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void Merge(uint3 id : SV_DispatchThreadID)
{
    rc_cascade_params params = LoadPassParams(rc_cascade_params);

    uint child  = params.Cascade;
    uint parent = child - 1;

    uint parentProbeSize = RC_CASCADE_PROBE_SIZE(parent);
    uint parentDirRes    = RC_CASCADE_DIR_RES(parent);
    uint childProbeSize  = RC_CASCADE_PROBE_SIZE(child);

    if (id.x >= RC_TILE_SIZE || id.y >= RC_TILE_SIZE || id.z >= parentProbeSize)
    {
        return;
    }

    float4 nearField = GiCascadeRW(parent)[id];

    if (nearField.a <= 0.0)
    {
        return;
    }

    uint2 probeXY = id.xy / parentDirRes;
    uint2 dirUV   = id.xy % parentDirRes;

    float3 grid = (float3(probeXY, id.z) + 0.5) * 0.5 - 0.5;

    probe_gather gather = ProbeCorners(grid, childProbeSize);

    float free[8];

    for (uint c = 0; c < 8; ++c)
    {
        free[c] = saturate(1.0 - ProbeOccupancy(gather.Corner[c], childProbeSize));
    }

    WeightProbes(gather, free);

    uint childDirRes = parentDirRes * 2;

    float4 farField = float4(0.0, 0.0, 0.0, 0.0);

    for (uint dv = 0; dv < 2; ++dv)
    {
        for (uint du = 0; du < 2; ++du)
        {
            uint2 childDir = dirUV * 2 + uint2(du, dv);

            for (uint tap = 0; tap < 8; ++tap)
            {
                uint3 coord = uint3(gather.Corner[tap].xy * childDirRes + childDir, gather.Corner[tap].z);

                farField += GiCascadeRW(child)[coord] * gather.Weight[tap];
            }
        }
    }

    farField *= ProbeNorm(gather) * 0.25;

    GiCascadeRW(parent)[id] = float4(nearField.rgb + nearField.a * farField.rgb, nearField.a * farField.a);
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Resolve(uint3 id : SV_DispatchThreadID)
{
    uint probeSize   = RC_CASCADE_PROBE_SIZE(RC_SCREEN_HANDOFF);
    uint dirRes      = RC_CASCADE_DIR_RES(RC_SCREEN_HANDOFF);
    uint cascadeSlot = VOLUME_SLOT_CASCADE + RC_SCREEN_HANDOFF;

    if (any(id >= probeSize))
    {
        return;
    }

    axis_light light = AxisLightZero();

    int size = (int)probeSize;

    float covered = 0.0;

    for (uint candidate = 0; candidate < LIGHT_DIRECTIONS + 1; ++candidate)
    {
        int3 offset = (candidate == 0) ? int3(0, 0, 0) : (int3)LightAxis[candidate - 1];

        int3 probe = int3(id) + offset;

        if (any(probe < 0) || any(probe >= size))
        {
            continue;
        }

        float free = saturate(1.0 - ProbeOccupancy((uint3)probe, probeSize));

        float probeWeight = free * ((candidate == 0) ? 2.0 : 1.0);

        if (probeWeight <= 1e-3)
        {
            continue;
        }

        AccumulateProbe(cascadeSlot, dirRes, (uint3)probe, probeWeight, light);

        covered += probeWeight;
    }

    if (covered <= 1e-3)
    {
        AccumulateProbe(cascadeSlot, dirRes, id, 1.0, light);
    }

    for (uint store = 0; store < LIGHT_DIRECTIONS; ++store)
    {
        GiIrradianceRW(store)[id] = AxisResolve(light, store);
    }
}

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void Prefilter(uint3 id : SV_DispatchThreadID)
{
    uint srcRes = RC_CASCADE_DIR_RES(RC_HANDOFF_CASCADE);
    uint dstRes = RC_SCREEN_DIR_RES;

    if (id.x >= RC_HANDOFF_TILE_SIZE || id.y >= RC_HANDOFF_TILE_SIZE || id.z >= RC_HANDOFF_PROBE_SIZE)
    {
        return;
    }

    uint2 probeXY = id.xy / dstRes;
    uint2 dirUV   = id.xy % dstRes;

    uint ratio = srcRes / dstRes;

    float4 total = float4(0.0, 0.0, 0.0, 0.0);

    for (uint v = 0; v < ratio; ++v)
    {
        for (uint u = 0; u < ratio; ++u)
        {
            uint2 src = probeXY * srcRes + dirUV * ratio + uint2(u, v);

            total += GiCascadeRW(RC_HANDOFF_CASCADE)[uint3(src, id.z)];
        }
    }

    GiHandoffRW[id] = total / (float)(ratio * ratio);
}
