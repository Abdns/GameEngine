#include "ShaderInterop.h"

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D Volumes[MAX_VOLUMES];

[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];
[[vk::binding(BINDING_SAMPLER,  SET_GLOBAL)]] SamplerState Samp;
[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

float3 SampleBounce(uint slot, float3 local, float3 normal)
{
    float3 uvw = LocalToUVW(local);

    if (any(uvw < 0.0) || any(uvw > 1.0))
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 total       = float3(0.0, 0.0, 0.0);
    float  totalWeight = 0.0;

    for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
    {
        float weight = max(dot(normal, LightAxis[axis]), 0.0);

        total       += Volumes[slot + axis].SampleLevel(VolumeSamp, SmoothUVW(uvw, (float)RC_IRRADIANCE_SIZE), 0).rgb * weight;
        totalWeight += weight;
    }

    return total / max(totalWeight, 1e-4);
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    rc_inject_params params = LoadRcInjectParams(pc.ParamsPtr);

    if (any(id >= params.LightSize))
    {
        return;
    }

    float4 history = VolumesRW[params.RadianceSlot][id];

    float4 solid = VolumesRW[params.SolidSlot][id];

    if (solid.a <= 0.0)
    {
        VolumesRW[params.RadianceSlot][id] = lerp(history, float4(0.0, 0.0, 0.0, 0.0), LIGHT_BLEND);
        return;
    }

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    float3 normal = float3(0.0, 1.0, 0.0);

    float3 packed  = VolumesRW[params.NormalSlot][id].rgb * 2.0 - 1.0;
    float  length2 = dot(packed, packed);

    if (length2 > 1e-6)
    {
        normal = packed * rsqrt(length2);
    }

    float skyVisibility = VolumesRW[params.SkySlot][id].r;

    uint  skyIndex = min(globals.SkyCubemap, (uint)(MAX_CUBEMAPS - 1));
    float lastMip  = max((float)globals.SkyMipCount - 1.0, 0.0);

    float3 skyColor = Sky[skyIndex].SampleLevel(Samp, normal, lastMip).rgb * LIGHT_SKY_STRENGTH;

    float3 toLight = normalize(globals.LightDir);

    float ndotl = max(dot(normal, toLight), 0.0);

    float voxelSize = (2.0 * VOLUME_WORLD_EXTENT) / (float)params.LightSize;

    float3 position = RcProbeLocal(id, params.LightSize);

    float sunVisibility = 1.0;

    if (ndotl > 0.0)
    {
        float stepSize = voxelSize;

        float3 origin = position + normal * voxelSize * 1.5 + toLight * voxelSize * 1.5;

        for (uint i = 0; i < RC_SUN_STEPS; ++i)
        {
            float3 uvw = LocalToUVW(origin + toLight * (stepSize * ((float)i + 0.5)));

            if (any(uvw < 0.0) || any(uvw > 1.0))
            {
                break;
            }

            float fringe = Volumes[params.SolidSlot].SampleLevel(VolumeSamp, uvw, 0).a;

            float blocker = saturate((fringe - RC_SUN_FRINGE) / (1.0 - RC_SUN_FRINGE));

            if (blocker > 0.001)
            {
                sunVisibility *= exp(-blocker * RC_EXTINCTION * stepSize);

                if (sunVisibility < 0.01)
                {
                    sunVisibility = 0.0;
                    break;
                }
            }
        }
    }

    float3 sunlight = globals.LightColor * ndotl * RC_INV_PI * sunVisibility;
    float3 skylight = skyColor * skyVisibility;

    float3 local = position + normal * voxelSize;

    float3 bounced = SampleBounce(params.IrradianceSlot, local, normal) * LIGHT_BOUNCE_STRENGTH;

    float4 current = float4(solid.rgb * (sunlight + skylight + bounced), solid.a);

    VolumesRW[params.RadianceSlot][id] = lerp(history, current, LIGHT_BLEND);
}
