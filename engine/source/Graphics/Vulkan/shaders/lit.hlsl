#include "interop/ShaderResources.h"

static const float PI = 3.14159265358979323846;

struct vs_output
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float3 Color         : COLOR0;
	[[vk::location(1)]] float2 UV            : TEXCOORD0;
	[[vk::location(2)]] float3 WorldPosition : TEXCOORD1;
	[[vk::location(3)]] float3 WorldNormal   : TEXCOORD2;
};

float3 SafeNormalize(float3 v)
{
	return v * rsqrt(max(dot(v, v), 1e-20));
}

float3 TransformNormal(float3x3 Model, float3 Normal)
{
	float3x3 Cofactors = float3x3(cross(Model[1], Model[2]), cross(Model[2], Model[0]), cross(Model[0], Model[1]));
	float Det = dot(Model[0], Cofactors[0]);

	return mul(Cofactors, Normal) * (Det < 0.0 ? -1.0 : 1.0);
}

float D_GGX(float3 N, float3 H, float roughness)
{
	float a = max(roughness * roughness, 0.001);
	float a2 = a * a;

	float NoH = saturate(dot(N, H));
	float d = NoH * NoH * (a2 - 1.0) + 1.0;

	return a2 / (PI * d * d);
}

float G_SmithGGX(float3 N, float3 V, float3 L, float roughness)
{
	float NoV = saturate(dot(N, V));
	float NoL = saturate(dot(N, L));

	float a = max(roughness * roughness, 0.001);
	float a2 = a * a;

	float termV = NoL * sqrt(a2 + (1.0 - a2) * NoV * NoV);
	float termL = NoV * sqrt(a2 + (1.0 - a2) * NoL * NoL);

	return (2.0 * NoV * NoL) / max(termV + termL, 1e-7);
}

float3 F_Schlick(float VoH, float3 F0)
{
	float f = pow(1.0 - saturate(VoH), 5.0);
	return F0 + (1.0 - F0) * f;
}

float3 DiffuseBRDF(float3 diffuseColor)
{
	return diffuseColor / PI;
}

float3 SpecularBRDF(float D, float G, float3 F, float NoV, float NoL)
{
	return (D * G * F) / max(4.0 * NoV * NoL, 1e-7);
}

float3 BRDF(float3 N, float3 V, float3 L, float3 baseColor, float roughness, float metallic)
{
	float NoV = saturate(dot(N, V));
	float NoL = saturate(dot(N, L));

	if (NoV <= 0.0 || NoL <= 0.0) return float3(0.0, 0.0, 0.0);

	float3 H = SafeNormalize(V + L);

	roughness = clamp(roughness, 0.04, 1.0);
	metallic = saturate(metallic);

	float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
	float D = D_GGX(N, H, roughness);
	float G = G_SmithGGX(N, V, L, roughness);
	float3 F = F_Schlick(dot(V, H), F0);
	float3 kD = (1.0 - F) * (1.0 - metallic);

	float3 diffuse = DiffuseBRDF(baseColor) * kD;
	float3 specular = SpecularBRDF(D, G, F, NoV, NoL);

	return diffuse + specular;
}

vs_output VSMain(uint vertexID : SV_VertexID)
{
	draw_params params = LoadDrawParams(draw_params);
	vertex v = LoadVertex(params.Vertices, vertexID);
	frame_globals globals = LoadGlobals(pc.GlobalsPtr);

	vs_output output;
	float4 WorldPosition = mul(params.Model, float4(v.Position, 1.0));

	output.Position = mul(globals.ViewProj, WorldPosition);
	output.Color = v.Color;
	output.UV = v.UV;
	output.WorldPosition = WorldPosition.xyz;
	output.WorldNormal = TransformNormal((float3x3)params.Model, v.Normal);

	return output;
}

float4 PSMain(vs_output input) : SV_Target
{
	frame_globals globals = LoadGlobals(pc.GlobalsPtr);

	float3 N = SafeNormalize(input.WorldNormal);
	float3 V = SafeNormalize(globals.CameraPosition - input.WorldPosition);
	float3 L = SafeNormalize(globals.LightDir);

	float3 baseColor = input.Color;
	float roughness = 0.5;
	float metallic = 0.0;
	float3 lightRadiance = float3(1.0, 1.0, 1.0);

	float NoL = saturate(dot(N, L));
	float3 color = BRDF(N, V, L, baseColor, roughness, metallic) * lightRadiance * NoL;

	return float4(color, 1.0);
}
