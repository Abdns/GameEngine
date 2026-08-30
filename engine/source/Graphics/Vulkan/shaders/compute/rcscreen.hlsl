#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUMES,  SET_GLOBAL)]] Texture3D    Volumes[MAX_VOLUMES];
[[vk::binding(BINDING_TEXTURES, SET_GLOBAL)]] Texture2D    Tex[TEXTURE_HEAP_SIZE];
[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

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

[numthreads(RC_SCREEN_GROUP, RC_SCREEN_GROUP, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
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

    float depthRight = 0.0;
    float depthLeft  = 0.0;
    float depthDown  = 0.0;
    float depthUp    = 0.0;

    float3 worldRight = SampleWorld(globals, params.DepthSlot, (int2)pixel + int2(RC_SCREEN_NORMAL_TAP, 0), depthRight);
    float3 worldLeft  = SampleWorld(globals, params.DepthSlot, (int2)pixel - int2(RC_SCREEN_NORMAL_TAP, 0), depthLeft);
    float3 worldDown  = SampleWorld(globals, params.DepthSlot, (int2)pixel + int2(0, RC_SCREEN_NORMAL_TAP), depthDown);
    float3 worldUp    = SampleWorld(globals, params.DepthSlot, (int2)pixel - int2(0, RC_SCREEN_NORMAL_TAP), depthUp);

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

    float cellSize = (2.0 * VOLUME_WORLD_EXTENT) / (float)LIGHT_GRID_SIZE;

    float3 local = center - globals.VolumeCenter + normal * cellSize * 1.5;

    float3 grid = LocalToUVW(local) * (float)params.ProbeSize - 0.5;

    int3   gridBase     = (int3)floor(grid);
    float3 gridFraction = saturate(grid - (float3)gridBase);

    gridFraction = gridFraction * gridFraction * (3.0 - 2.0 * gridFraction);

    uint3 cascadeCorner[8];
    float cascadeWeight[8];
    float cascadeTotal = 0.0;

    for (uint c = 0; c < 8; ++c)
    {
        int3 offset = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);

        float3 axisWeight = lerp(1.0 - gridFraction, gridFraction, (float3)offset);

        int3 probe = clamp(gridBase + offset, int3(0, 0, 0), (int3)(params.ProbeSize - 1));

        float3 probeLocal = RcProbeLocal((uint3)probe, params.ProbeSize);

        float handoffCell = (2.0 * VOLUME_WORLD_EXTENT) / (float)params.ProbeSize;

        float side = dot(probeLocal - local, normal);

        cascadeCorner[c] = (uint3)probe;
        cascadeWeight[c] = axisWeight.x * axisWeight.y * axisWeight.z * saturate(side / (0.25 * handoffCell) + 0.5);
        cascadeTotal    += cascadeWeight[c];
    }

    if (cascadeTotal < 1e-4)
    {
        cascadeTotal = 0.0;

        for (uint fallback = 0; fallback < 8; ++fallback)
        {
            int3 offset = int3(fallback & 1, (fallback >> 1) & 1, (fallback >> 2) & 1);

            float3 axisWeight = lerp(1.0 - gridFraction, gridFraction, (float3)offset);

            cascadeWeight[fallback] = axisWeight.x * axisWeight.y * axisWeight.z;
            cascadeTotal           += cascadeWeight[fallback];
        }
    }

    float cascadeNorm = 1.0 / max(cascadeTotal, 1e-4);

    float3 total[LIGHT_DIRECTIONS];
    float  skyTotal[LIGHT_DIRECTIONS];
    float  weights[LIGHT_DIRECTIONS];

    for (uint clear = 0; clear < LIGHT_DIRECTIONS; ++clear)
    {
        total[clear]    = float3(0.0, 0.0, 0.0);
        skyTotal[clear] = 0.0;
        weights[clear]  = 0.0;
    }

    float stepSize = params.IntervalLength / (float)params.Steps;

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

            float3 radiance      = float3(0.0, 0.0, 0.0);
            float  transmittance = 1.0;

            for (uint i = 0; i < params.Steps; ++i)
            {
                float travel = stepSize * ((float)i + 0.5);

                float3 uvw = LocalToUVW(local + direction * travel);

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

            float4 farField = float4(0.0, 0.0, 0.0, 0.0);

            for (uint dv = 0; dv < ratio; ++dv)
            {
                for (uint du = 0; du < ratio; ++du)
                {
                    uint2 dirUV = uint2(u, v) * ratio + uint2(du, dv);

                    for (uint tap = 0; tap < 8; ++tap)
                    {
                        uint3 coord = uint3(cascadeCorner[tap].xy * params.DirRes + dirUV, cascadeCorner[tap].z);

                        farField += VolumesRW[params.CascadeSlot][coord] * cascadeWeight[tap];
                    }
                }
            }

            farField *= cascadeNorm / (float)(ratio * ratio);

            float3 merged   = radiance + transmittance * farField.rgb;
            float  skyReach = transmittance * farField.a;

            for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
            {
                float weight = max(dot(LightAxis[axis], direction), 0.0);

                total[axis]    += merged * weight;
                skyTotal[axis] += skyReach * weight;
                weights[axis]  += weight;
            }
        }
    }

    float viewDepth = ViewDistance(globals, depth);

    VolumesRW[params.MetaSlot][uint3(id.xy, 0)] = float4(normal, viewDepth);

    for (uint store = 0; store < LIGHT_DIRECTIONS; ++store)
    {
        float norm = 1.0 / max(weights[store], 1e-4);

        VolumesRW[params.ScreenSlot + store][uint3(id.xy, 0)] = float4(total[store] * norm, saturate(skyTotal[store] * norm));
    }
}
