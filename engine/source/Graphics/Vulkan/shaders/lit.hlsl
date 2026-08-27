#include "ShaderInterop.h"

[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];
[[vk::binding(BINDING_VOLUMES,  SET_GLOBAL)]] Texture3D    Volumes[MAX_VOLUMES];
[[vk::binding(BINDING_SAMPLER,  SET_GLOBAL)]] SamplerState Samp;

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

float3 SampleLightGrid(float3 worldPos, float3 normal, float3 center, float3 skyIrradiance)
{
    float cellSize = (2.0 * VOXEL_WORLD_EXTENT) / (float)LIGHT_GRID_SIZE;

    float3 samplePos = worldPos + normal * cellSize * 0.5 - center;
    float3 UVW       = (samplePos + VOXEL_WORLD_EXTENT) / (2.0 * VOXEL_WORLD_EXTENT);

    if (any(UVW < 0.0) || any(UVW > 1.0))
    {
        return skyIrradiance;
    }

    float3 total       = float3(0.0, 0.0, 0.0);
    float  totalWeight = 0.0;

    for (uint direction = 0; direction < LIGHT_DIRECTIONS; ++direction)
    {
        float weight = max(dot(normal, -LightAxis[direction]), 0.0) + LIGHT_READ_AMBIENT;

        total       += Volumes[VOLUME_SLOT_LIGHT_HISTORY + direction].SampleLevel(Samp, UVW, 0).rgb * weight;
        totalWeight += weight;
    }

    float3 bounce = total * (LIGHT_GI_STRENGTH / max(totalWeight, 1e-4));

    float skyVisibility = Volumes[VOLUME_SLOT_SKYVIS].SampleLevel(Samp, UVW, 0).r;

    return bounce + skyIrradiance * skyVisibility;
}

float3 SampleLightGridRay(float3 worldPos, float3 direction, float3 center)
{
    float cellSize = (2.0 * VOXEL_WORLD_EXTENT) / (float)LIGHT_GRID_SIZE;

    float3 samplePos = worldPos + direction * cellSize - center;
    float3 UVW       = (samplePos + VOXEL_WORLD_EXTENT) / (2.0 * VOXEL_WORLD_EXTENT);

    if (any(UVW < 0.0) || any(UVW > 1.0))
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 posFar = worldPos + direction * cellSize * 2.5 - center;
    float3 UVWFar = clamp((posFar + VOXEL_WORLD_EXTENT) / (2.0 * VOXEL_WORLD_EXTENT), 0.0, 1.0);

    float3 total       = float3(0.0, 0.0, 0.0);
    float  totalWeight = 0.0;

    for (uint bucket = 0; bucket < LIGHT_DIRECTIONS; ++bucket)
    {
        float weight = max(dot(direction, -LightAxis[bucket]), 0.0);

        weight *= weight;

        float3 closeTap  = Volumes[VOLUME_SLOT_LIGHT_HISTORY + bucket].SampleLevel(Samp, UVW, 0).rgb;
        float3 distantTap = Volumes[VOLUME_SLOT_LIGHT_HISTORY + bucket].SampleLevel(Samp, UVWFar, 0).rgb;

        total       += (closeTap + distantTap) * 0.5 * weight;
        totalWeight += weight;
    }

    return total * (LIGHT_GI_STRENGTH / max(totalWeight, 1e-4));
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

    float3 N = dot(Ngeom, V) < 0.0 ? -Ngeom : Ngeom;

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

    uint   SkyIndex   = min(globals.SkyCubemap, (uint)(MAX_CUBEMAPS - 1));
    float  LastMip    = max((float)globals.SkyMipCount - 1.0, 0.0);
    float3 SkyIrradiance = Sky[SkyIndex].SampleLevel(Samp, N, LastMip).rgb;
    float3 Irradiance    = SampleLightGrid(input.WorldPos, Ngeom, globals.VoxelCenter, SkyIrradiance);

    float3 Reflection = OffSpecularPeakDirection(N, reflect(-V, N), Roughness);
    float3 Radiance   = Sky[SkyIndex].SampleLevel(Samp, Reflection, RoughnessToMip(Roughness, LastMip)).rgb;

    Radiance += SampleLightGridRay(input.WorldPos, Reflection, globals.VoxelCenter);

    float3 Ambient = Albedo * Irradiance + Radiance * EnvironmentBRDF(F0, Roughness, NdotV);

    float3 Color = Direct + Ambient;

    return float4(Color, Material.BaseColor.a * params.Tint.a);
}
