#include "Compute.hlsl"

groupshared probe_gather MergeGather;

probe_gather VisibleCascadeProbes(float3 origin, float3 grid, uint probeSize)
{
    probe_gather gather = ProbeCorners(grid, probeSize);
    float visible[8];

    for (uint c = 0; c < 8; ++c)
    {
        visible[c] = 0.0;
        if (gather.Weight[c] > 0.0 && ProbeOccupancy(gather.Corner[c], probeSize) <= 0.0)
        {
            float3 probeOrigin = RcProbeLocal(gather.Corner[c], probeSize);
            visible[c] = GiSegmentVisibility(origin, probeOrigin);
        }
    }

    WeightProbes(gather, visible);
    return gather;
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

    uint3 probe = uint3(id.xy / dirRes, id.z);
    uint2 dirUV = id.xy % dirRes;

    if (ProbeOccupancy(probe, probeSize) > 0.0)
    {
        GiCascadeRW(cascade)[id] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 origin    = RcProbeLocal(probe, probeSize);
    float3 direction = RcDirection(dirUV, dirRes);

    float start = 0.0;
    float span  = 0.0;
    RcCascadeInterval(cascade, start, span);

    if (cascade == RC_CASCADE_COUNT - 1)
    {
        // Escaped rays mean visible sky only after the remaining domain is clear.
        span = VolumeTraceDistance();
    }

    GiCascadeRW(cascade)[id] = GiTraceSurface(origin, direction, start, span);
}

[numthreads(RC_GROUP_SIZE, RC_GROUP_SIZE, 1)]
void Merge(uint3 id : SV_DispatchThreadID, uint3 thread : SV_GroupThreadID)
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

    uint3 parentProbe = uint3(id.xy / parentDirRes, id.z);
    uint2 dirUV       = id.xy % parentDirRes;
    float3 origin    = RcProbeLocal(parentProbe, parentProbeSize);
    float3 grid      = ((float3)parentProbe + 0.5) * 0.5 - 0.5;

    probe_gather gather;
    if (parentDirRes >= RC_GROUP_SIZE)
    {
        // Angular tiles align to thread groups, so these lanes share a probe
        // origin. Trace its eight visibility segments once, not once per ray.
        if (all(thread == 0))
        {
            MergeGather = VisibleCascadeProbes(origin, grid, childProbeSize);
        }
        GroupMemoryBarrierWithGroupSync();
        gather = MergeGather;
    }
    else
    {
        gather = VisibleCascadeProbes(origin, grid, childProbeSize);
    }

    float4 nearField = GiCascadeRW(parent)[id];
    if (nearField.a <= 0.0)
    {
        return;
    }

    float4 farField = float4(0.0, 0.0, 0.0, 0.0);

    if (gather.Total <= 1e-4)
    {
        // Rejected probes stay rejected. Continue this ray from the same origin
        // instead of borrowing radiance across an opaque wall.
        float start = 0.0;
        float span  = 0.0;
        RcCascadeInterval(child, start, span);
        farField = GiTraceSurface(origin, RcDirection(dirUV, parentDirRes), start, VolumeTraceDistance());
    }
    else
    {
        uint childDirRes = parentDirRes * 2;
        float angularWeight = 0.0;

        for (uint dv = 0; dv < 2; ++dv)
        {
            for (uint du = 0; du < 2; ++du)
            {
                uint2 childDir = dirUV * 2 + uint2(du, dv);
                float solidAngle = RcDirectionSolidAngle(childDir, childDirRes);

                for (uint tap = 0; tap < 8; ++tap)
                {
                    if (gather.Weight[tap] <= 0.0)
                    {
                        continue;
                    }
                    uint3 coord = uint3(gather.Corner[tap].xy * childDirRes + childDir, gather.Corner[tap].z);
                    farField += GiCascadeRW(child)[coord] * (gather.Weight[tap] * solidAngle);
                }
                angularWeight += solidAngle;
            }
        }

        farField *= ProbeNorm(gather) / max(angularWeight, 1e-6);
    }

    GiCascadeRW(parent)[id] = float4(nearField.rgb + nearField.a * farField.rgb, nearField.a * farField.a);
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Resolve(uint3 id : SV_DispatchThreadID)
{
    uint probeSize = RC_CASCADE_PROBE_SIZE(RC_SCREEN_HANDOFF);
    uint dirRes    = RC_CASCADE_DIR_RES(RC_SCREEN_HANDOFF);

    if (any(id >= probeSize))
    {
        return;
    }

    axis_light light = AxisLightZero();

    if (ProbeOccupancy(id, probeSize) <= 0.0)
    {
        float3 origin = RcProbeLocal(id, probeSize);
        float start = 0.0;
        float span  = 0.0;
        RcCascadeInterval(RC_SCREEN_HANDOFF, start, span);

        // Resolve only this probe. Spatial interpolation and visibility rejection
        // happen at the receiver; a neighbor blur would erase contact lighting.
        for (uint v = 0; v < dirRes; ++v)
        {
            for (uint u = 0; u < dirRes; ++u)
            {
                uint2 dirUV = uint2(u, v);
                float3 direction = RcDirection(dirUV, dirRes);
                uint3 coord = uint3(id.xy * dirRes + dirUV, id.z);

                // The first allocated cascade starts beyond zero. Its missing
                // near interval belongs in world irradiance, not in its stored
                // cascade, because screen probes trace that near interval too.
                float4 nearField = GiTraceSurface(origin, direction, 0.0, start);
                float4 farField  = GiCascadeRW(RC_SCREEN_HANDOFF)[coord];

                // Spatially shifted cascade rays are useful radiance estimates,
                // but their blockers need not lie on this probe's actual ray.
                // Validate inherited occlusion to avoid multiplying unrelated
                // directional shadows across the cascades.
                if (nearField.a > 0.0 && farField.a < 0.9999)
                {
                    farField = GiTraceSurface(origin, direction, start, VolumeTraceDistance());
                }
                float3 radiance  = nearField.rgb + nearField.a * farField.rgb;
                float skyReach   = nearField.a * farField.a;

                AxisAccumulate(light, direction, radiance, skyReach, RcDirectionSolidAngle(dirUV, dirRes));
            }
        }
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
    uint ratio   = srcRes / dstRes;

    float4 total = float4(0.0, 0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    for (uint v = 0; v < ratio; ++v)
    {
        for (uint u = 0; u < ratio; ++u)
        {
            uint2 srcDir = dirUV * ratio + uint2(u, v);
            uint2 src = probeXY * srcRes + srcDir;
            float weight = RcDirectionSolidAngle(srcDir, srcRes);

            total += GiCascadeRW(RC_HANDOFF_CASCADE)[uint3(src, id.z)] * weight;
            totalWeight += weight;
        }
    }

    GiHandoffRW[id] = total / max(totalWeight, 1e-6);
}
