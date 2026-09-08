#include "Compute.hlsl"

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Inject(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= LIGHT_GRID_SIZE)) return;
    float4 surface = GiAlbedo.Load(int4(id, 0));
    if (surface.a <= 0.0)
    {
        // Removed geometry stops emitting and occluding in this frame.
        GiRadianceRW[id] = 0.0;
        return;
    }
    frame_globals globals = LoadGlobals(pc.GlobalsPtr);
    float4 encoded = GiNormal.Load(int4(id, 0));
    float3 normal = normalize(encoded.rgb * 2.0 - 1.0);
    float3 local = RcProbeLocal(id, LIGHT_GRID_SIZE);
    float3 toLight = normalize(globals.LightDir);
    float sunVisibility = GiInjectionSunVisibility(local, normal, toLight);
    float3 sunlight = globals.LightColor * max(dot(normal, toLight), 0.0) * RC_INV_PI * sunVisibility;

    // Previous-frame irradiance supplies one additional diffuse bounce.
    // Geometry and the visibility rays are always from the current frame.
    float skyVisibility;
    float3 bounced = GiSampleDiffuseProbes(local, normal, skyVisibility);
    float3 skylight = GiDiffuseSky(globals, normal) * skyVisibility;
    float3 current = surface.rgb * 0.96 * (sunlight + skylight + bounced * LIGHT_BOUNCE_STRENGTH);

    float3 history = GiRadianceRW[id].rgb;
    float response = globals.GiHistorySeconds > 0.0
        ? 1.0 - exp(-max(globals.GiDeltaTime, 0.0) / globals.GiHistorySeconds) : 1.0;
    if (encoded.a <= 0.0) response = 1.0;
    GiRadianceRW[id] = float4(lerp(history, current, response), surface.a);
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Smooth(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= LIGHT_GRID_SIZE)) return;
    float4 center = GiRadiance.Load(int4(id, 0));
    if (center.a <= 0.0)
    {
        GiRadianceSmoothRW[id] = 0.0;
        return;
    }
    float3 normal = normalize(GiNormal.Load(int4(id, 0)).rgb * 2.0 - 1.0);
    float3 albedo = GiAlbedo.Load(int4(id, 0)).rgb;
    float3 sum = center.rgb * 2.0;
    float total = 2.0;
    [unroll] for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
    {
        int3 cell = (int3)id + (int3)LightAxis[axis];
        if (any(cell < 0) || any(cell >= LIGHT_GRID_SIZE)) continue;
        float4 neighbor = GiRadiance.Load(int4(cell, 0));
        if (neighbor.a <= 0.0) continue;
        float3 n = normalize(GiNormal.Load(int4(cell, 0)).rgb * 2.0 - 1.0);
        float3 a = GiAlbedo.Load(int4(cell, 0)).rgb;
        float plane = abs(dot(LightAxis[axis], normal));
        float materialDifference = max(abs(a.r - albedo.r), max(abs(a.g - albedo.g), abs(a.b - albedo.b)));
        // Smooth along the same material/plane, never into air or across walls.
        float weight = saturate((dot(n, normal) - 0.95) * 20.0)
                     * saturate(1.0 - plane * 4.0)
                     * saturate(1.0 - materialDifference * 16.0);
        sum += neighbor.rgb * weight;
        total += weight;
    }
    GiRadianceSmoothRW[id] = float4(sum / total, center.a);
}
