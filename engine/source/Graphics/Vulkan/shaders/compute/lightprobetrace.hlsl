#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUMES,  SET_GLOBAL)]] Texture3D    Volumes[MAX_VOLUMES];
[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];
[[vk::binding(BINDING_SAMPLER,  SET_GLOBAL)]] SamplerState Samp;

[[vk::push_constant]] push_constants pc;

static const float GOLDEN_ANGLE = 2.399963;

float3 FibonacciDirection(uint index)
{
    float z   = 1.0 - 2.0 * ((float)index + 0.5) / (float)PROBE_RAY_SET;
    float phi = (float)index * GOLDEN_ANGLE;
    float r   = sqrt(saturate(1.0 - z * z));

    return float3(r * cos(phi), z, r * sin(phi));
}

float3 LocalToUVW(float3 local)
{
    return (local + VOXEL_WORLD_EXTENT) / (2.0 * VOXEL_WORLD_EXTENT);
}

bool MarchField(float3 origin, float3 direction, out float3 hitPoint)
{
    float fineVoxel = (2.0 * VOXEL_WORLD_EXTENT) / (float)VOXEL_GRID_SIZE;

    float travel = fineVoxel;

    for (uint i = 0; i < PROBE_MARCH_STEPS; ++i)
    {
        float3 samplePos = origin + direction * travel;
        float3 UVW       = LocalToUVW(samplePos);

        if (any(UVW < 0.0) || any(UVW > 1.0))
        {
            break;
        }

        float dist = Volumes[VOLUME_SLOT_SDF].SampleLevel(Samp, UVW, 0).r;

        if (dist < fineVoxel * 0.5)
        {
            hitPoint = samplePos + direction * fineVoxel * 0.5;
            return true;
        }

        travel += max(dist, fineVoxel * 0.5);
    }

    hitPoint = origin;
    return false;
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    trace_params params = LoadTraceParams(pc.ParamsPtr);

    if (any(id >= params.LightSize))
    {
        return;
    }

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    float lightVoxel = (2.0 * VOXEL_WORLD_EXTENT) / (float)params.LightSize;
    float fineVoxel  = (2.0 * VOXEL_WORLD_EXTENT) / (float)VOXEL_GRID_SIZE;

    float3 probe = ((float3)id + 0.5) * lightVoxel - VOXEL_WORLD_EXTENT;

    for (uint attempt = 0; attempt < 4; ++attempt)
    {
        float3 UVW   = LocalToUVW(probe);
        int3   coord = clamp(int3(UVW * VOXEL_GRID_SIZE), 0, VOXEL_GRID_SIZE - 1);

        if (Volumes[VOLUME_SLOT_ALBEDO].Load(int4(coord, 0)).a <= 0.0)
        {
            break;
        }

        float3 packed = Volumes[VOLUME_SLOT_NORMAL].Load(int4(coord, 0)).rgb * 2.0 - 1.0;

        float length2 = dot(packed, packed);

        float3 escape = length2 > 1e-6 ? packed * rsqrt(length2) : float3(0.0, 1.0, 0.0);

        probe += escape * fineVoxel * 1.5;
    }

    float3 bucketLight[LIGHT_DIRECTIONS];
    float  bucketWeight[LIGHT_DIRECTIONS];

    for (uint reset = 0; reset < LIGHT_DIRECTIONS; ++reset)
    {
        bucketLight[reset]  = float3(0.0, 0.0, 0.0);
        bucketWeight[reset] = 0.0;
    }

    uint  skyIndex = min(globals.SkyCubemap, (uint)(MAX_CUBEMAPS - 1));
    float lastMip  = max((float)globals.SkyMipCount - 1.0, 0.0);

    float3 skyTop  = Sky[skyIndex].SampleLevel(Samp, float3(0.0, 1.0, 0.0), lastMip).rgb * LIGHT_SKY_STRENGTH;
    float3 toLight = normalize(globals.LightDir);

    uint stride = PROBE_RAY_SET / PROBE_RAYS_PER_FRAME;
    uint offset = (globals.FrameIndex + id.x + id.y * 3 + id.z * 7) % stride;

    for (uint ray = 0; ray < PROBE_RAYS_PER_FRAME; ++ray)
    {
        float3 direction = FibonacciDirection(ray * stride + offset);

        float3 hitPoint;

        float3 radiance = float3(0.0, 0.0, 0.0);

        if (MarchField(probe, direction, hitPoint))
        {
            float3 hitUVW   = LocalToUVW(hitPoint);
            int3   hitCoord = clamp(int3(hitUVW * VOXEL_GRID_SIZE), 0, VOXEL_GRID_SIZE - 1);

            float4 albedo = Volumes[VOLUME_SLOT_ALBEDO].Load(int4(hitCoord, 0));

            float3 packed  = Volumes[VOLUME_SLOT_NORMAL].Load(int4(hitCoord, 0)).rgb * 2.0 - 1.0;
            float  length2 = dot(packed, packed);
            float3 normal  = length2 > 1e-6 ? packed * rsqrt(length2) : float3(0.0, 1.0, 0.0);

            float skyVisibility = Volumes[VOLUME_SLOT_SKYVIS].SampleLevel(Samp, hitUVW, 0).r;

            float3 shadowStart = hitPoint + normal * fineVoxel * 2.0;
            float3 unusedHit;

            float sunShadow = MarchField(shadowStart, toLight, unusedHit) ? 0.0 : 1.0;

            float3 sunlight = globals.LightColor * max(dot(normal, toLight), 0.0) * sunShadow;
            float3 skylight = skyTop * skyVisibility * saturate(normal.y * 0.5 + 0.5);

            float3 bounced     = float3(0.0, 0.0, 0.0);
            float  bounceTotal = 0.0;

            for (uint incoming = 0; incoming < LIGHT_DIRECTIONS; ++incoming)
            {
                float weight = max(dot(normal, -LightAxis[incoming]), 0.0);

                bounced     += Volumes[VOLUME_SLOT_LIGHT_HISTORY + incoming].SampleLevel(Samp, hitUVW, 0).rgb * weight;
                bounceTotal += weight;
            }

            bounced *= LIGHT_BOUNCE_STRENGTH / max(bounceTotal, 1e-4);

            radiance = albedo.rgb * (sunlight + skylight + bounced);
        }

        float3 lightDirection = -direction;

        for (uint bucket = 0; bucket < LIGHT_DIRECTIONS; ++bucket)
        {
            float weight = max(dot(lightDirection, LightAxis[bucket]), 0.0);

            bucketLight[bucket]  += radiance * weight;
            bucketWeight[bucket] += weight;
        }
    }

    for (uint stored = 0; stored < LIGHT_DIRECTIONS; ++stored)
    {
        if (bucketWeight[stored] < 1e-4)
        {
            continue;
        }

        float3 estimate = bucketLight[stored] / bucketWeight[stored];

        float blend = LIGHT_BLEND * saturate(bucketWeight[stored]);

        float4 history = VolumesRW[params.LightSlot + stored][id];

        VolumesRW[params.LightSlot + stored][id] = float4(lerp(history.rgb, estimate, blend), 1.0);
    }
}
