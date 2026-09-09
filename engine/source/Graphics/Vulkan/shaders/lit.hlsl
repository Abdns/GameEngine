#include "ShaderInterop.h"

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
    draw_params params = LoadPassParams(draw_params);
    vertex v = LoadVertex(params.Vertices, vertexID);
    frame_globals globals = LoadGlobals(pc.GlobalsPtr);
    float4 worldPos = mul(params.Model, float4(v.Position, 1.0));

    vs_output output;
    output.Position = mul(globals.ViewProj, worldPos);
    output.WorldPos = worldPos.xyz;
    output.Normal   = TransformNormal(params.Model, v.Normal);
    return output;
}

float DistributionGGX(float roughness, float nDotH)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float VisibilitySmith(float roughness, float nDotV, float nDotL)
{
    float a = roughness * roughness;
    float v = lerp(2.0 * nDotL * nDotV, nDotL + nDotV, a);
    return 0.5 / max(v, 1e-4);
}

float3 FresnelSchlick(float3 f0, float vDotH)
{
    float f = 1.0 - vDotH;
    float f5 = f * f * f * f * f;
    return f0 + (1.0 - f0) * f5;
}

float GeometricRoughness(float3 normal, float roughness)
{
    float3 deltaU = ddx(normal);
    float3 deltaV = ddy(normal);
    float variance = dot(deltaU, deltaU) + dot(deltaV, deltaV);
    return min(roughness + min(2.0 * variance, GSAA_MAX_BIAS), 1.0);
}

float3 EnvironmentBRDF(float3 f0, float roughness, float nDotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572,  0.022);
    const float4 c1 = float4( 1.0,  0.0425,  1.040, -0.040);

    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    float singleScatter = max(ab.x + ab.y, 1e-3);
    float3 compensation = 1.0 + f0 * (1.0 / singleScatter - 1.0);
    return (f0 * ab.x + ab.y) * compensation;
}

float4 PSMain(vs_output input) : SV_Target
{
    draw_params params = LoadPassParams(draw_params);
    frame_globals globals = LoadGlobals(pc.GlobalsPtr);
    gpu_material material = LoadMaterial(params.Materials, params.MaterialSlot);

    float3 baseColor = saturate(material.BaseColor.rgb * params.Tint.rgb);
    float metallic = saturate(material.Metallic);
    float roughness = clamp(material.Roughness, 0.045, 1.0);
    float3 normal = normalize(input.Normal);
    roughness = GeometricRoughness(normal, roughness);

    float3 viewDirection = normalize(globals.CameraPos - input.WorldPos);
    float3 lightDirection = normalize(globals.LightDir);
    float3 halfDirection = normalize(viewDirection + lightDirection);

    float nDotV = saturate(dot(normal, viewDirection)) + 1e-5;
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotH = saturate(dot(normal, halfDirection));
    float vDotH = saturate(dot(viewDirection, halfDirection));

    float3 albedo = baseColor * (1.0 - metallic);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 fresnel = FresnelSchlick(f0, vDotH);
    float distribution = DistributionGGX(roughness, nDotH);
    float visibility = VisibilitySmith(roughness, nDotV, nDotL);

    float3 direct = ((1.0 - fresnel) * albedo / PI + fresnel * distribution * visibility)
                  * globals.LightColor * nDotL;

    uint skyIndex = min(globals.SkyCubemap, (uint)(MAX_CUBEMAPS - 1));
    float lastMip = (float)max((int)globals.SkyMipCount - 1, 0);
    float3 environmentDiffuse = Sky[skyIndex].SampleLevel(Samp, normal, lastMip).rgb;
    float3 reflection = reflect(-viewDirection, normal);
    float3 environmentSpecular = Sky[skyIndex].SampleLevel(Samp, reflection, roughness * lastMip).rgb;
    float3 specularWeight = saturate(EnvironmentBRDF(f0, roughness, nDotV));
    float3 ambient = albedo * (1.0 - specularWeight) * environmentDiffuse
                   + environmentSpecular * specularWeight;

    return float4(direct + ambient, material.BaseColor.a * params.Tint.a);
}
