#include "ShaderInterop.h"

[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];
[[vk::binding(BINDING_VOLUMES,  SET_GLOBAL)]] Texture3D    Volumes[MAX_VOLUMES];
[[vk::binding(BINDING_SAMPLER,        SET_GLOBAL)]] SamplerState Samp;
[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

static const float PI            = 3.14159265;
static const float GSAA_MAX_BIAS = 0.2;

struct vs_output
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 WorldPos : TEXCOORD0;
    [[vk::location(1)]] float3 Normal   : TEXCOORD1;
};

vs_output VSMain(uint vertexID : SV_VertexID)
{
    draw_params params = LoadDrawParams(pc.ParamsPtr);

    vertex v = LoadVertex(params.Vertices, vertexID);

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    float4 WorldPos = mul(params.Model, float4(v.Position, 1.0));

    vs_output output;
    output.Position = mul(globals.ViewProj, WorldPos);
    output.WorldPos = WorldPos.xyz;
    output.Normal   = mul((float3x3)params.Model, v.Normal);
    return output;
}

float DistributionGGX(float Roughness, float NdotH)
{
    float a  = Roughness * Roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;

    return a2 / max(PI * d * d, 1e-6);
}

float VisibilitySmith(float Roughness, float NdotV, float NdotL)
{
    float a = Roughness * Roughness;
    float v = lerp(2.0 * NdotL * NdotV, NdotL + NdotV, a);

    return 0.5 / max(v, 1e-4);
}

float3 FresnelSchlick(float3 F0, float VdotH)
{
    float f = 1.0 - VdotH;
    float f5 = f * f * f * f * f;

    return F0 + (1.0 - F0) * f5;
}

float GeometricRoughness(float3 Normal, float Roughness)
{
    float3 DeltaU = ddx(Normal);
    float3 DeltaV = ddy(Normal);

    float Variance = dot(DeltaU, DeltaU) + dot(DeltaV, DeltaV);

    return min(Roughness + min(2.0 * Variance, GSAA_MAX_BIAS), 1.0);
}

float RoughnessToMip(float Roughness, float LastMip)
{
    return Roughness * (1.7 - 0.7 * Roughness) * LastMip;
}

float3 OffSpecularPeakDirection(float3 Normal, float3 Reflection, float Roughness)
{
    float a = Roughness * Roughness;

    return normalize(lerp(Reflection, Normal, a));
}

float3 EnvironmentBRDF(float3 F0, float Roughness, float NdotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572,  0.022);
    const float4 c1 = float4( 1.0,  0.0425,  1.040, -0.040);

    float4 r    = Roughness * c0 + c1;
    float  a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    float2 ab   = float2(-1.04, 1.04) * a004 + r.zw;

    float  SingleScatter = max(ab.x + ab.y, 1e-3);
    float3 Compensation  = 1.0 + F0 * (1.0 / SingleScatter - 1.0);

    return (F0 * ab.x + ab.y) * Compensation;
}

float3 SampleCascadeProbes(float3 uvw, float3 direction)
{
    float3 total       = float3(0.0, 0.0, 0.0);
    float  totalWeight = 0.0;

    for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
    {
        float weight = max(dot(direction, LightAxis[axis]), 0.0);

        total       += Volumes[VOLUME_SLOT_IRRADIANCE + axis].SampleLevel(VolumeSamp, SmoothUVW(uvw, (float)RC_IRRADIANCE_SIZE), 0).rgb * weight;
        totalWeight += weight;
    }

    return total * (LIGHT_GI_STRENGTH / max(totalWeight, 1e-4));
}

float3 SampleScreenBounce(float2 pixel, float viewDepth, float3 normal, uint2 probeCount, out float coverage)
{
    float2 coord = pixel / (float)RC_SCREEN_TILE - 0.5;

    int2   base     = (int2)floor(coord);
    float2 fraction = coord - (float2)base;

    fraction = fraction * fraction * (3.0 - 2.0 * fraction);

    float3 total       = float3(0.0, 0.0, 0.0);
    float  totalWeight = 0.0;

    for (uint corner = 0; corner < 4; ++corner)
    {
        int2 offset = int2(corner & 1, corner >> 1);
        int2 probe  = base + offset;

        if (any(probe < 0) || probe.x >= (int)probeCount.x || probe.y >= (int)probeCount.y)
        {
            continue;
        }

        float4 stored[LIGHT_DIRECTIONS];

        for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
        {
            stored[axis] = Volumes[VOLUME_SLOT_SCREEN_GI + axis].Load(int4(probe, 0, 0));
        }

        if (stored[0].a <= 0.0)
        {
            continue;
        }

        float3 probeNormal = float3(stored[1].a, stored[2].a, stored[3].a);

        float2 axisWeight = lerp(1.0 - fraction, fraction, (float2)offset);

        float depthWeight  = saturate(1.0 - abs(stored[0].a - viewDepth) / max(viewDepth * 0.05, 1e-3));
        float normalWeight = saturate(dot(probeNormal, normal));

        normalWeight *= normalWeight;

        float weight = axisWeight.x * axisWeight.y * depthWeight * normalWeight;

        if (weight <= 0.0)
        {
            continue;
        }

        float3 value       = float3(0.0, 0.0, 0.0);
        float  valueWeight = 0.0;

        for (uint lobe = 0; lobe < LIGHT_DIRECTIONS; ++lobe)
        {
            float aligned = max(dot(normal, LightAxis[lobe]), 0.0);

            value       += stored[lobe].rgb * aligned;
            valueWeight += aligned;
        }

        total       += value * (weight / max(valueWeight, 1e-4));
        totalWeight += weight;
    }

    coverage = totalWeight;

    return totalWeight > 1e-4 ? total * (LIGHT_GI_STRENGTH / totalWeight) : float3(0.0, 0.0, 0.0);
}


float3 SampleIrradiance(float3 worldPos, float3 normal, float3 center, float3 skyIrradiance, float3 screenBounce, float coverage)
{
    float probeSpacing = (2.0 * VOLUME_WORLD_EXTENT) / (float)RC_PROBE_SIZE;
    float voxelSize    = (2.0 * VOLUME_WORLD_EXTENT) / (float)VOLUME_GRID_SIZE;

    float3 local = worldPos - center;
    float3 uvw   = LocalToUVW(local + normal * probeSpacing * 0.5);

    if (any(uvw < 0.0) || any(uvw > 1.0))
    {
        return skyIrradiance;
    }

    float visibility = Volumes[VOLUME_SLOT_SKY_OCCLUSION].SampleLevel(VolumeSamp, SmoothUVW(LocalToUVW(local + normal * voxelSize), (float)VOLUME_GRID_SIZE), 0).r;

    float3 skylight = skyIrradiance * lerp(1.0, visibility, LIGHT_OCCLUSION_STRENGTH);

    float3 bounce = coverage > 1e-4 ? screenBounce : SampleCascadeProbes(uvw, normal);

    return bounce + skylight;
}

float3 SampleIrradianceRay(float3 worldPos, float3 direction, float3 center)
{
    float probeSpacing = (2.0 * VOLUME_WORLD_EXTENT) / (float)RC_PROBE_SIZE;

    float3 uvw = LocalToUVW(worldPos + direction * probeSpacing * 0.5 - center);

    if (any(uvw < 0.0) || any(uvw > 1.0))
    {
        return float3(0.0, 0.0, 0.0);
    }

    return SampleCascadeProbes(uvw, direction);
}

float4 PSMain(vs_output input) : SV_Target
{
    draw_params params = LoadDrawParams(pc.ParamsPtr);

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    gpu_material Material = LoadMaterial(params.Materials, params.MaterialSlot);

    float3 BaseColor = Material.BaseColor.rgb * params.Tint.rgb;
    float  Metallic  = saturate(Material.Metallic);
    float  Roughness = clamp(Material.Roughness, 0.045, 1.0);

    Roughness = GeometricRoughness(input.Normal, Roughness);

    float3 Ngeom = normalize(input.Normal);
    float3 V     = normalize(globals.CameraPos - input.WorldPos);

    float3 N = Ngeom;

    float3 L = normalize(globals.LightDir);
    float3 H = normalize(V + L);

    float NdotV = saturate(dot(N, V)) + 1e-5;
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 Albedo = BaseColor * (1.0 - Metallic);
    float3 F0     = lerp(float3(0.04, 0.04, 0.04), BaseColor, Metallic);

    float3 Fresnel    = FresnelSchlick(F0, VdotH);
    float  Normalized = DistributionGGX(Roughness, NdotH);
    float  Visibility = VisibilitySmith(Roughness, NdotV, NdotL);

    float3 Direct = (Albedo / PI + Fresnel * Normalized * Visibility) * globals.LightColor * NdotL;

    uint   SkyIndex      = min(globals.SkyCubemap, (uint)(MAX_CUBEMAPS - 1));
    float  LastMip       = max((float)globals.SkyMipCount - 1.0, 0.0);
    float3 SkyIrradiance = Sky[SkyIndex].SampleLevel(Samp, N, LastMip).rgb;
    float  ViewDepth  = mul(globals.ViewProj, float4(input.WorldPos, 1.0)).w;
    uint2  ProbeCount = uint2((globals.ScreenWidth + RC_SCREEN_TILE - 1) / RC_SCREEN_TILE, (globals.ScreenHeight + RC_SCREEN_TILE - 1) / RC_SCREEN_TILE);

    float  Coverage     = 0.0;
    float3 ScreenBounce = SampleScreenBounce(input.Position.xy, ViewDepth, Ngeom, ProbeCount, Coverage);

    float3 Irradiance = SampleIrradiance(input.WorldPos, Ngeom, globals.VolumeCenter, SkyIrradiance, ScreenBounce, Coverage);

    float3 Reflection = OffSpecularPeakDirection(N, reflect(-V, N), Roughness);
    float3 SkyRadiance = Sky[SkyIndex].SampleLevel(Samp, Reflection, RoughnessToMip(Roughness, LastMip)).rgb;

    float3 Radiance = SkyRadiance + SampleIrradianceRay(input.WorldPos, Reflection, globals.VolumeCenter);

    float3 Ambient = Albedo * Irradiance + Radiance * EnvironmentBRDF(F0, Roughness, NdotV);

    float3 Color = Direct + Ambient;

    return float4(Color, Material.BaseColor.a * params.Tint.a);
}
