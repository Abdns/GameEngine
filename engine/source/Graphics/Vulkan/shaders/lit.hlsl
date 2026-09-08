#include "ShaderInterop.h"
#include "interop/GiSampling.h"

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

    float4 WorldPos = mul(params.Model, float4(v.Position, 1.0));

    vs_output output;
    output.Position = mul(globals.ViewProj, WorldPos);
    output.WorldPos = WorldPos.xyz;
    output.Normal   = TransformNormal(params.Model, v.Normal);
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

float3 ScreenProbeWorld(frame_globals globals, int2 probe, float viewDepth)
{
    int2 screenSize = int2((int)globals.ScreenWidth, (int)globals.ScreenHeight);
    int2 pixel = min(probe * RC_SCREEN_TILE + RC_SCREEN_TILE / 2, screenSize - 1);
    float2 ndc = ((float2)pixel + 0.5) / (float2)screenSize * 2.0 - 1.0;
    float3 ray = globals.SkyRight.xyz * ndc.x + globals.SkyUp.xyz * ndc.y + globals.SkyForward.xyz;

    return globals.CameraPos + ray * viewDepth;
}

float3 SampleScreenBounce(frame_globals globals, float2 pixel, float3 worldPos, float viewDepth,
                          float3 normal, float pixelFootprint, out float confidence, out float skyVisibility)
{
    uint2 probeCount = uint2(RC_SCREEN_PROBES_X(globals.ScreenWidth), RC_SCREEN_PROBES_Y(globals.ScreenHeight));
    float2 coord = (pixel - 0.5 - (float)(RC_SCREEN_TILE / 2)) / (float)RC_SCREEN_TILE;

    int2   base     = (int2)floor(coord);
    float2 lastPixel = float2((float)globals.ScreenWidth, (float)globals.ScreenHeight) - 0.5;
    float2 firstCenter = min((float2)base * RC_SCREEN_TILE + (float)(RC_SCREEN_TILE / 2) + 0.5, lastPixel);
    float2 nextCenter = min(((float2)base + 1.0) * RC_SCREEN_TILE + (float)(RC_SCREEN_TILE / 2) + 0.5, lastPixel);
    float2 fraction = saturate((pixel - firstCenter) / max(nextCenter - firstCenter, 1.0));

    fraction = fraction * fraction * (3.0 - 2.0 * fraction);

    float voxelSize = (2.0 * VOLUME_WORLD_EXTENT) / (float)VOLUME_GRID_SIZE;
    float planeTolerance = max(voxelSize * 0.125, min(pixelFootprint * 2.0, voxelSize * 0.5));
    float depthTolerance = max(viewDepth * 0.01, pixelFootprint * (float)RC_SCREEN_TILE * 2.0);

    float3 total       = float3(0.0, 0.0, 0.0);
    float  skyTotal    = 0.0;
    float  totalWeight = 0.0;

    for (uint corner = 0; corner < 4; ++corner)
    {
        int2 offset = int2(corner & 1, corner >> 1);
        int2 probe  = base + offset;

        if (any(probe < 0) || probe.x >= (int)probeCount.x || probe.y >= (int)probeCount.y)
        {
            continue;
        }

        float4 meta = GiScreenMeta.Load(int4(probe, 0, 0));

        if (meta.a <= 0.0)
        {
            continue;
        }

        float2 axisWeight = lerp(1.0 - fraction, fraction, (float2)offset);

        float3 probeWorld = ScreenProbeWorld(globals, probe, meta.a);
        float3 delta = probeWorld - worldPos;
        float planeDistance = max(abs(dot(delta, normal)), abs(dot(delta, meta.rgb)));

        float planeWeight = saturate(1.0 - planeDistance / max(planeTolerance, 1e-4));
        // Adjacent pixels on a sloped plane legitimately differ in depth. Keep
        // full confidence within that footprint; plane distance rejects leaks.
        float depthWeight = saturate(2.0 - abs(meta.a - viewDepth) / max(depthTolerance, 1e-3));
        float normalWeight = saturate((dot(meta.rgb, normal) - 0.8) / 0.2);

        normalWeight *= normalWeight;

        float weight = axisWeight.x * axisWeight.y * planeWeight * depthWeight * normalWeight;

        if (weight <= 0.0)
        {
            continue;
        }

        // Screen probes store diffuse E/pi for their surface normal, plus sky visibility.
        float4 stored = GiScreenLight.Load(int4(probe, 0, 0));

        total       += stored.rgb * weight;
        skyTotal    += stored.a * weight;
        totalWeight += weight;
    }

    confidence = saturate(totalWeight);

    if (totalWeight <= 1e-4)
    {
        skyVisibility = 1.0;
        return float3(0.0, 0.0, 0.0);
    }

    float norm = 1.0 / totalWeight;

    skyVisibility = saturate(skyTotal * norm);

    return total * norm;
}

float3 HistoryDebug(float3 local, float3 normal)
{
    float voxelSize = (2.0 * VOLUME_WORLD_EXTENT) / (float)VOLUME_GRID_SIZE;

    for (uint tap = 0; tap < 3; ++tap)
    {
        float offset = (tap == 0) ? 0.0 : ((tap == 1) ? -0.5 : 0.5);
        float3 uvw = LocalToUVW(local + normal * (offset * voxelSize));

        if (any(uvw < 0.0) || any(uvw >= 1.0))
        {
            continue;
        }

        int3 coord = (int3)floor(uvw * (float)VOLUME_GRID_SIZE);

        if (GiAlbedo.Load(int4(coord, 0)).a > 0.5)
        {
            float valid = saturate(GiNormal.Load(int4(coord, 0)).a);
            return float3(1.0 - valid, valid, 0.0);
        }
    }

    // Blue means there is no occupied voxel close enough to diagnose this surface.
    return float3(0.0, 0.0, 1.0);
}

float4 PSMain(vs_output input) : SV_Target
{
    draw_params params = LoadPassParams(draw_params);

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    gpu_material Material = LoadMaterial(params.Materials, params.MaterialSlot);

    float3 BaseColor = saturate(Material.BaseColor.rgb * params.Tint.rgb);
    float  Metallic  = saturate(Material.Metallic);
    float  Roughness = clamp(Material.Roughness, 0.045, 1.0);

    float3 N = normalize(input.Normal);
    float3 derivativeX = ddx(input.WorldPos);
    float3 derivativeY = ddy(input.WorldPos);
    float3 geometric = cross(derivativeX, derivativeY);
    float geometricLength2 = dot(geometric, geometric);
    float3 Ngeom = geometricLength2 > 1e-12 ? geometric * rsqrt(geometricLength2) : N;
    Ngeom = dot(Ngeom, N) < 0.0 ? -Ngeom : Ngeom;

    Roughness = GeometricRoughness(N, Roughness);

    float3 local = input.WorldPos - globals.VolumeCenter;
    float3 V = normalize(globals.CameraPos - input.WorldPos);

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

    float3 Direct = ((1.0 - Fresnel) * Albedo / PI + Fresnel * Normalized * Visibility)
                  * globals.LightColor * NdotL;

    float ViewDepth = mul(globals.ViewProj, float4(input.WorldPos, 1.0)).w;
    float PixelFootprint = max(length(derivativeX), length(derivativeY));
    float Confidence = 0.0;
    float ScreenSkyVis = 1.0;
    float3 ScreenBounce = SampleScreenBounce(globals, input.Position.xy, input.WorldPos, ViewDepth,
                                           Ngeom, PixelFootprint, Confidence, ScreenSkyVis);
    ScreenBounce *= LIGHT_GI_STRENGTH;

    float WorldSkyVis = ScreenSkyVis;
    float3 WorldBounce = ScreenBounce;
    if (Confidence < 0.999)
    {
        // Find free space along the actual surface normal; evaluate light with
        // the shading normal without shifting the receiver into a neighbor.
        WorldBounce = GiSampleDiffuseProbes(local, N, Ngeom, WorldSkyVis);
    }
    float3 Bounce = lerp(WorldBounce, ScreenBounce, Confidence) * globals.GiStrength;
    float SkyVisibility = saturate(lerp(WorldSkyVis, ScreenSkyVis, Confidence));
    float3 DiffuseSky = GiDiffuseSky(globals, N) * SkyVisibility;

    float3 Reflection = reflect(-V, N);
    // Smooth shading normals can reflect below the actual triangle at grazing
    // angles. Keep the sky direction above that geometric plane.
    Reflection = normalize(Reflection + Ngeom * max(0.001 - dot(Reflection, Ngeom), 0.0));
    float3 SkyRadiance = GiSpecularSky(globals, Reflection, Roughness);

    float3 SpecularWeight = saturate(EnvironmentBRDF(F0, Roughness, NdotV));
    float3 DiffuseWeight = Albedo * (1.0 - SpecularWeight);
    // Both diffuse fields contain E/pi; the Lambertian pi is already accounted for.
    float3 Indirect = DiffuseWeight * (Bounce + DiffuseSky);

    float3 Radiance = SkyRadiance;
    float3 Reflections = Radiance * SpecularWeight;

    float3 Color = Direct + Indirect + Reflections;

    if (globals.GiDebugMode == GI_DEBUG_DIRECT)
    {
        Color = Direct;
    }
    else if (globals.GiDebugMode == GI_DEBUG_INDIRECT)
    {
        Color = Indirect;
    }
    else if (globals.GiDebugMode == GI_DEBUG_SKY_VISIBILITY)
    {
        Color = SkyVisibility.xxx;
    }
    else if (globals.GiDebugMode == GI_DEBUG_SCREEN_CONFIDENCE)
    {
        Color = float3(1.0 - Confidence, Confidence, 0.0);
    }
    else if (globals.GiDebugMode == GI_DEBUG_HISTORY_REJECTION)
    {
        Color = HistoryDebug(local, Ngeom);
    }
    else if (globals.GiDebugMode == GI_DEBUG_REFLECTIONS)
    {
        Color = Reflections;
    }

    return float4(Color, Material.BaseColor.a * params.Tint.a);
}
