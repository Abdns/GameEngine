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

    uint2 pixel = id.xy * RC_SCREEN_TILE + RC_SCREEN_TILE / 2;

    float depth = Tex[TEXTURE_SLOT_DEPTH].Load(int3((int2)pixel, 0)).r;

    if (depth >= 1.0)
    {
        for (uint sky = 0; sky < LIGHT_DIRECTIONS; ++sky)
        {
            VolumesRW[VOLUME_SLOT_SCREEN_GI + sky][uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 1.0);
        }

        VolumesRW[VOLUME_SLOT_SCREEN_META][uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 0.0);

        return;
    }

    float3 center = ReconstructWorld(globals, pixel, depth);

    float3 normal = ReconstructNormal(globals, pixel, depth, center);

    float cellSize = (2.0 * VOLUME_WORLD_EXTENT) / (float)LIGHT_GRID_SIZE;

    float3 local = center - globals.VolumeCenter + normal * cellSize * 1.5;

    float3 grid = LocalToUVW(local) * (float)RC_HANDOFF_PROBE_SIZE - 0.5;

    probe_gather gather = ProbeCorners(grid, RC_HANDOFF_PROBE_SIZE);

    float handoffCell = (2.0 * VOLUME_WORLD_EXTENT) / (float)RC_HANDOFF_PROBE_SIZE;

    float side[8];

    for (uint c = 0; c < 8; ++c)
    {
        float3 probeLocal = RcProbeLocal(gather.Corner[c], RC_HANDOFF_PROBE_SIZE);

        side[c] = saturate(dot(probeLocal - local, normal) / (0.25 * handoffCell) + 0.5);
    }

    WeightProbes(gather, side);

    float cascadeNorm = ProbeNorm(gather);

    axis_light light = AxisLightZero();

    float intervalStart = 0.0;
    float intervalSpan  = 0.0;

    RcCascadeInterval(RC_HANDOFF_CASCADE, intervalStart, intervalSpan);

    for (uint v = 0; v < RC_SCREEN_DIR_RES; ++v)
    {
        for (uint u = 0; u < RC_SCREEN_DIR_RES; ++u)
        {
            float3 direction = RcDirection(uint2(u, v), RC_SCREEN_DIR_RES);

            if (dot(direction, normal) <= 0.0)
            {
                continue;
            }

            float4 nearField = TraceVolume(VOLUME_SLOT_RADIANCE_SMOOTH, local, direction, 0.0, intervalStart, RC_CASCADE_STEPS(RC_HANDOFF_CASCADE));

            float4 farField = float4(0.0, 0.0, 0.0, 0.0);

            for (uint tap = 0; tap < 8; ++tap)
            {
                uint3 coord = uint3(gather.Corner[tap].xy * RC_SCREEN_DIR_RES + uint2(u, v), gather.Corner[tap].z);

                farField += VolumesRW[VOLUME_SLOT_HANDOFF][coord] * gather.Weight[tap];
            }

            farField *= cascadeNorm;

            float3 merged   = nearField.rgb + nearField.a * farField.rgb;
            float  skyReach = nearField.a * farField.a;

            AxisAccumulate(light, direction, merged, skyReach, 1.0);
        }
    }

    float viewDepth = ViewDistance(globals, depth);

    VolumesRW[VOLUME_SLOT_SCREEN_META][uint3(id.xy, 0)] = float4(normal, viewDepth);

    for (uint store = 0; store < LIGHT_DIRECTIONS; ++store)
    {
        VolumesRW[VOLUME_SLOT_SCREEN_GI + store][uint3(id.xy, 0)] = AxisResolve(light, store);
    }
}
