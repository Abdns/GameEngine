#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];
[[vk::binding(BINDING_SAMPLER,  SET_GLOBAL)]] SamplerState Samp;

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    inject_params params = LoadInjectParams(pc.ParamsPtr);

    if (any(id >= params.LightSize))
    {
        return;
    }

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    uint  ratio = VOXEL_GRID_SIZE / LIGHT_GRID_SIZE;
    uint3 fine  = id * ratio + ratio / 2;

    float skyVisibility = VolumesRW[VOLUME_SLOT_SKYVIS][fine].r;

    float4 solid = VolumesRW[params.SolidSlot][id];

    float3 radiance = float3(0.0, 0.0, 0.0);
    float3 normal   = float3(0.0, 1.0, 0.0);

    if (solid.a > 0.0)
    {
        float3 packed  = VolumesRW[params.NormalSlot][id].rgb * 2.0 - 1.0;
        float  length2 = dot(packed, packed);

        if (length2 > 1e-6)
        {
            normal = packed * rsqrt(length2);
        }

        uint  skyIndex = min(globals.SkyCubemap, (uint)(MAX_CUBEMAPS - 1));
        float lastMip  = max((float)globals.SkyMipCount - 1.0, 0.0);

        float3 skyColor = Sky[skyIndex].SampleLevel(Samp, float3(0.0, 1.0, 0.0), lastMip).rgb * LIGHT_SKY_STRENGTH;

        float3 toLight = normalize(globals.LightDir);

        float3 sunlight = globals.LightColor * max(dot(normal, toLight), 0.0);
        float3 skylight = skyColor * skyVisibility * saturate(normal.y * 0.5 + 0.5);

        float3 bounced     = float3(0.0, 0.0, 0.0);
        float  bounceTotal = 0.0;

        for (uint incoming = 0; incoming < LIGHT_DIRECTIONS; ++incoming)
        {
            float weight = max(dot(normal, -LightAxis[incoming]), 0.0);

            bounced     += VolumesRW[VOLUME_SLOT_LIGHT_HISTORY + incoming][id].rgb * weight;
            bounceTotal += weight;
        }

        bounced *= LIGHT_BOUNCE_STRENGTH / max(bounceTotal, 1e-4);

        radiance = solid.rgb * (sunlight + skylight + bounced);
    }

    float weights[LIGHT_DIRECTIONS];
    float total = 0.0;

    for (uint direction = 0; direction < LIGHT_DIRECTIONS; ++direction)
    {
        weights[direction] = max(dot(normal, LightAxis[direction]), 0.0);
        total += weights[direction];
    }

    float normalization = total > 1e-4 ? 1.0 / total : 0.0;

    for (uint stored = 0; stored < LIGHT_DIRECTIONS; ++stored)
    {
        float4 injected = float4(radiance * weights[stored] * normalization, 0.0);

        VolumesRW[params.LightSlot + stored][id] = injected;
        VolumesRW[params.SumSlot + stored][id]   = injected;
    }
}
