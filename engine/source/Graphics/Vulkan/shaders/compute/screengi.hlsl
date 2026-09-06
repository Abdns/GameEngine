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

float3 SampleWorld(frame_globals globals, uint depthSlot, int2 pixel, out float depth)
{
    int2 clamped = clamp(pixel, int2(0, 0), int2((int)globals.ScreenWidth - 1, (int)globals.ScreenHeight - 1));

    depth = Tex[depthSlot].Load(int3(clamped, 0)).r;

    return ReconstructWorld(globals, (uint2)clamped, depth);
}

float3 ReconstructNormal(frame_globals globals, uint depthSlot, uint2 pixel, float depth, float3 center)
{
    float depthRight = 0.0;
    float depthLeft  = 0.0;
    float depthDown  = 0.0;
    float depthUp    = 0.0;

    float3 worldRight = SampleWorld(globals, depthSlot, (int2)pixel + int2(RC_SCREEN_NORMAL_TAP, 0), depthRight);
    float3 worldLeft  = SampleWorld(globals, depthSlot, (int2)pixel - int2(RC_SCREEN_NORMAL_TAP, 0), depthLeft);
    float3 worldDown  = SampleWorld(globals, depthSlot, (int2)pixel + int2(0, RC_SCREEN_NORMAL_TAP), depthDown);
    float3 worldUp    = SampleWorld(globals, depthSlot, (int2)pixel - int2(0, RC_SCREEN_NORMAL_TAP), depthUp);

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
    rc_screen_params params = LoadRcScreenParams(pc.ParamsPtr);

    if (id.x >= params.ProbeCountX || id.y >= params.ProbeCountY)
    {
        return;
    }

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    uint2 pixel = id.xy * RC_SCREEN_TILE + RC_SCREEN_TILE / 2;

    float depth = Tex[params.DepthSlot].Load(int3((int2)pixel, 0)).r;

    if (depth >= 1.0)
    {
        for (uint sky = 0; sky < LIGHT_DIRECTIONS; ++sky)
        {
            VolumesRW[params.ScreenSlot + sky][uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 1.0);
        }

        VolumesRW[params.MetaSlot][uint3(id.xy, 0)] = float4(0.0, 0.0, 0.0, 0.0);

        return;
    }

    float3 center = ReconstructWorld(globals, pixel, depth);

    float3 normal = ReconstructNormal(globals, params.DepthSlot, pixel, depth, center);

    float cellSize = (2.0 * VOLUME_WORLD_EXTENT) / (float)LIGHT_GRID_SIZE;

    float3 local = center - globals.VolumeCenter + normal * cellSize * 1.5;

    float3 grid = LocalToUVW(local) * (float)params.ProbeSize - 0.5;

    probe_gather gather = ProbeCorners(grid, params.ProbeSize);

    float handoffCell = (2.0 * VOLUME_WORLD_EXTENT) / (float)params.ProbeSize;

    float side[8];

    for (uint c = 0; c < 8; ++c)
    {
        float3 probeLocal = RcProbeLocal(gather.Corner[c], params.ProbeSize);

        side[c] = saturate(dot(probeLocal - local, normal) / (0.25 * handoffCell) + 0.5);
    }

    WeightProbes(gather, side);

    float cascadeNorm = ProbeNorm(gather);

    axis_light light = AxisLightZero();

    uint ratio = params.DirRes / RC_SCREEN_DIR_RES;

    for (uint v = 0; v < RC_SCREEN_DIR_RES; ++v)
    {
        for (uint u = 0; u < RC_SCREEN_DIR_RES; ++u)
        {
            float3 direction = RcDirection(uint2(u, v), RC_SCREEN_DIR_RES);

            if (dot(direction, normal) <= 0.0)
            {
                continue;
            }

            float4 nearField = TraceVolume(params.RadianceSlot, local, direction, 0.0, params.IntervalLength, params.Steps);

            float4 farField = float4(0.0, 0.0, 0.0, 0.0);

            for (uint dv = 0; dv < ratio; ++dv)
            {
                for (uint du = 0; du < ratio; ++du)
                {
                    uint2 dirUV = uint2(u, v) * ratio + uint2(du, dv);

                    for (uint tap = 0; tap < 8; ++tap)
                    {
                        uint3 coord = uint3(gather.Corner[tap].xy * params.DirRes + dirUV, gather.Corner[tap].z);

                        farField += VolumesRW[params.CascadeSlot][coord] * gather.Weight[tap];
                    }
                }
            }

            farField *= cascadeNorm / (float)(ratio * ratio);

            float3 merged   = nearField.rgb + nearField.a * farField.rgb;
            float  skyReach = nearField.a * farField.a;

            AxisAccumulate(light, direction, merged, skyReach, 1.0);
        }
    }

    float viewDepth = ViewDistance(globals, depth);

    VolumesRW[params.MetaSlot][uint3(id.xy, 0)] = float4(normal, viewDepth);

    for (uint store = 0; store < LIGHT_DIRECTIONS; ++store)
    {
        VolumesRW[params.ScreenSlot + store][uint3(id.xy, 0)] = AxisResolve(light, store);
    }
}
