#include "Compute.hlsl"

float ViewDistance(frame_globals globals, float depth)
{
    return (globals.CameraNear * globals.CameraFar) / (depth * (globals.CameraNear - globals.CameraFar) + globals.CameraFar);
}

float3 ReconstructWorld(frame_globals globals, uint2 pixel, float depth)
{
    float2 ndc = ((float2)pixel + 0.5) / float2((float)globals.ScreenWidth, (float)globals.ScreenHeight) * 2.0 - 1.0;

    float3 ray = globals.SkyRight.xyz * ndc.x + globals.SkyUp.xyz * ndc.y + globals.SkyForward.xyz;

    return globals.CameraPos + ray * ViewDistance(globals, depth);
}

float3 SampleWorld(frame_globals globals, int2 pixel, out float depth)
{
    int2 clamped = clamp(pixel, int2(0, 0), int2((int)globals.ScreenWidth - 1, (int)globals.ScreenHeight - 1));

    depth = Tex[TEXTURE_SLOT_DEPTH].Load(int3(clamped, 0)).r;

    return ReconstructWorld(globals, (uint2)clamped, depth);
}

float3 ReconstructNormal(frame_globals globals, uint2 pixel, float depth, float3 center)
{
    float depthRight = 0.0;
    float depthLeft  = 0.0;
    float depthDown  = 0.0;
    float depthUp    = 0.0;

    float3 worldRight = SampleWorld(globals, (int2)pixel + int2(RC_SCREEN_NORMAL_TAP, 0), depthRight);
    float3 worldLeft  = SampleWorld(globals, (int2)pixel - int2(RC_SCREEN_NORMAL_TAP, 0), depthLeft);
    float3 worldDown  = SampleWorld(globals, (int2)pixel + int2(0, RC_SCREEN_NORMAL_TAP), depthDown);
    float3 worldUp    = SampleWorld(globals, (int2)pixel - int2(0, RC_SCREEN_NORMAL_TAP), depthUp);

    float3 alongX = (abs(depthRight - depth) < abs(depthLeft - depth)) ? (worldRight - center) : (center - worldLeft);
    float3 alongY = (abs(depthDown - depth) < abs(depthUp - depth)) ? (worldDown - center) : (center - worldUp);

    float3 toCamera = globals.CameraPos - center;

    float3 normal  = cross(alongY, alongX);
    float  length2 = dot(normal, normal);

    normal = (length2 > 1e-12) ? normal * rsqrt(length2) : normalize(toCamera);

    if (dot(normal, toCamera) < 0.0)
    {
        normal = -normal;
    }

    return normal;
}

[numthreads(RC_SCREEN_GROUP, RC_SCREEN_GROUP, 1)]
void Probe(uint3 id : SV_DispatchThreadID)
{
    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    if (id.x >= RC_SCREEN_PROBES_X(globals.ScreenWidth) || id.y >= RC_SCREEN_PROBES_Y(globals.ScreenHeight))
    {
        return;
    }

    uint2 pixel = min(id.xy * RC_SCREEN_TILE + RC_SCREEN_TILE / 2,
                      uint2(globals.ScreenWidth - 1, globals.ScreenHeight - 1));

    float depth = Tex[TEXTURE_SLOT_DEPTH].Load(int3((int2)pixel, 0)).r;

    if (depth >= 1.0)
    {
        GiScreenLightRW[uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 1.0);
        GiScreenMetaRW[uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 0.0);

        return;
    }

    float3 center = ReconstructWorld(globals, pixel, depth);

    float3 normal = ReconstructNormal(globals, pixel, depth, center);

    float3 local = GiSurfaceOrigin(center - globals.VolumeCenter, normal);

    if (any(LocalToUVW(local) < 0.0) || any(LocalToUVW(local) > 1.0))
    {
        GiScreenLightRW[uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 1.0);
        GiScreenMetaRW[uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 grid = LocalToUVW(local) * (float)RC_HANDOFF_PROBE_SIZE - 0.5;

    probe_gather gather = ProbeCorners(grid, RC_HANDOFF_PROBE_SIZE);

    float handoffCell = (2.0 * VOLUME_WORLD_EXTENT) / (float)RC_HANDOFF_PROBE_SIZE;

    float side[8];

    for (uint c = 0; c < 8; ++c)
    {
        float3 probeLocal = RcProbeLocal(gather.Corner[c], RC_HANDOFF_PROBE_SIZE);

        side[c] = 0.0;
        if (gather.Weight[c] > 0.0 && ProbeOccupancy(gather.Corner[c], RC_HANDOFF_PROBE_SIZE) <= 0.0)
        {
            float planeWeight = saturate(dot(probeLocal - local, normal) / (0.25 * handoffCell) + 0.5);
            if (planeWeight > 0.0)
            {
                side[c] = planeWeight * GiSegmentVisibility(local, probeLocal, normal);
            }
        }
    }

    WeightProbes(gather, side);

    float cascadeNorm = ProbeNorm(gather);

    float3 total = float3(0.0, 0.0, 0.0);
    float skyTotal = 0.0;
    float totalWeight = 0.0;

    float intervalStart = 0.0;
    float intervalSpan  = 0.0;

    RcCascadeInterval(RC_HANDOFF_CASCADE, intervalStart, intervalSpan);

    for (uint v = 0; v < RC_SCREEN_DIR_RES; ++v)
    {
        for (uint u = 0; u < RC_SCREEN_DIR_RES; ++u)
        {
            uint2 dirUV = uint2(u, v);
            float3 direction = RcDirection(dirUV, RC_SCREEN_DIR_RES);
            float cosine = max(dot(direction, normal), 0.0);

            if (cosine <= 0.0)
            {
                continue;
            }

            float4 incoming;
            if (gather.Total <= 1e-4)
            {
                // A closed or thin room may contain no visible handoff probe.
                // Preserve occlusion by tracing locally instead of restoring
                // interpolation weights for probes behind its walls.
                incoming = GiTraceSurface(local, direction, 0.0, VolumeTraceDistance(), normal);
            }
            else
            {
                float4 nearField = GiTraceSurface(local, direction, 0.0, intervalStart, normal);
                float4 farField = float4(0.0, 0.0, 0.0, 0.0);

                if (nearField.a > 0.0)
                {
                    for (uint tap = 0; tap < 8; ++tap)
                    {
                        if (gather.Weight[tap] <= 0.0)
                        {
                            continue;
                        }
                        uint3 coord = uint3(gather.Corner[tap].xy * RC_SCREEN_DIR_RES + dirUV, gather.Corner[tap].z);
                        farField += GiHandoffRW[coord] * gather.Weight[tap];
                    }
                    farField *= cascadeNorm;

                    // A visible probe can still see a blocker in this angular
                    // bin that the receiver does not. Confirm inherited
                    // occlusion along the actual receiver ray before darkening
                    // it; preserve the inexpensive fully-open-sky case.
                    if (farField.a < 0.9999)
                    {
                        farField = GiTraceSurface(local, direction, intervalStart, VolumeTraceDistance(), normal);
                    }
                }
                incoming = float4(nearField.rgb + nearField.a * farField.rgb, nearField.a * farField.a);
            }

            float weight = cosine * RcDirectionSolidAngle(dirUV, RC_SCREEN_DIR_RES);
            total += incoming.rgb * weight;
            skyTotal += incoming.a * weight;
            totalWeight += weight;
        }
    }

    float viewDepth = ViewDistance(globals, depth);

    GiScreenMetaRW[uint3(id.xy, 0)] = float4(normal, viewDepth);

    // One diffuse E/pi value per surface probe. Normalized angular quadrature
    // preserves constant radiance and uses the actual receiver normal.
    float norm = 1.0 / max(totalWeight, 1e-4);
    GiScreenLightRW[uint3(id.xy, 0)] = float4(total * norm, saturate(skyTotal * norm));
}
