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

float ProbeOccupancy(uint slot, uint3 probe, uint probeSize)
{
    return Volumes[slot].SampleLevel(VolumeSamp, LocalToUVW(RcProbeLocal(probe, probeSize)), 0).a;
}

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void Trace(uint3 id : SV_DispatchThreadID)
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

    VolumesRW[params.CascadeSlot][id] = TraceVolume(params.RadianceSlot, origin, direction, params.IntervalStart, params.IntervalLength, params.Steps);
}

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void Merge(uint3 id : SV_DispatchThreadID)
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

    probe_gather gather = ProbeCorners(grid, params.ChildProbeSize);

    float free[8];

    for (uint c = 0; c < 8; ++c)
    {
        free[c] = saturate(1.0 - ProbeOccupancy(params.RadianceSlot, gather.Corner[c], params.ChildProbeSize));
    }

    WeightProbes(gather, free);

    uint childDirRes = params.ParentDirRes * 2;

    float4 farField = float4(0.0, 0.0, 0.0, 0.0);

    for (uint dv = 0; dv < 2; ++dv)
    {
        for (uint du = 0; du < 2; ++du)
        {
            uint2 childDir = dirUV * 2 + uint2(du, dv);

            for (uint tap = 0; tap < 8; ++tap)
            {
                uint3 coord = uint3(gather.Corner[tap].xy * childDirRes + childDir, gather.Corner[tap].z);

                farField += VolumesRW[params.ChildSlot][coord] * gather.Weight[tap];
            }
        }
    }

    farField *= ProbeNorm(gather) * 0.25;

    VolumesRW[params.ParentSlot][id] = float4(nearField.rgb + nearField.a * farField.rgb, nearField.a * farField.a);
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Resolve(uint3 id : SV_DispatchThreadID)
{
    rc_resolve_params params = LoadRcResolveParams(pc.ParamsPtr);

    if (any(id >= params.ProbeSize))
    {
        return;
    }

    axis_light light = AxisLightZero();

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

        AccumulateProbe(params.CascadeSlot, params.DirRes, (uint3)probe, probeWeight, light);

        covered += probeWeight;
    }

    if (covered <= 1e-3)
    {
        AccumulateProbe(params.CascadeSlot, params.DirRes, id, 1.0, light);
    }

    for (uint store = 0; store < LIGHT_DIRECTIONS; ++store)
    {
        VolumesRW[params.IrradianceSlot + store][id] = AxisResolve(light, store);
    }
}

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void Prefilter(uint3 id : SV_DispatchThreadID)
{
    volume_op_params params = LoadVolumeOpParams(pc.ParamsPtr);

    uint tile = params.Size * params.DstRes;

    if (id.x >= tile || id.y >= tile || id.z >= params.Size)
    {
        return;
    }

    uint2 probeXY = id.xy / params.DstRes;
    uint2 dirUV   = id.xy % params.DstRes;

    uint ratio = params.SrcRes / params.DstRes;

    float4 total = float4(0.0, 0.0, 0.0, 0.0);

    for (uint v = 0; v < ratio; ++v)
    {
        for (uint u = 0; u < ratio; ++u)
        {
            uint2 src = probeXY * params.SrcRes + dirUV * ratio + uint2(u, v);

            total += VolumesRW[params.SrcSlot][uint3(src, id.z)];
        }
    }

    VolumesRW[params.DstSlot][id] = total / (float)(ratio * ratio);
}
