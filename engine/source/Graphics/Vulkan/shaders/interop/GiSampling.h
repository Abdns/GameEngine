#ifndef GI_SAMPLING_H
#define GI_SAMPLING_H

// All positions in this file are relative to globals.VolumeCenter.
// Rays traverse CURRENT opaque geometry. Filtering/history never changes visibility.
float GiVoxelSize()
{
    return (2.0 * VOLUME_WORLD_EXTENT) / (float)VOLUME_GRID_SIZE;
}

float3 GiSurfaceOrigin(float3 local, float3 normal)
{
    float length2 = dot(normal, normal);
    if (length2 <= 1e-12 || !all(isfinite(normal))) return local;
    normal *= rsqrt(length2);

    float cellSize = GiVoxelSize();
    float epsilon = cellSize * 0.001;
    float budget = cellSize * GI_SURFACE_SEARCH_VOXELS;
    float3 nearOrigin = local + normal * epsilon;
    float3 origin = nearOrigin;
    float travel = epsilon;

    // Return the FIRST represented air cell. A fixed full-cell offset can jump
    // over a narrow gap into another object's shell and then walk into its interior.
    [unroll] for (uint i = 0; i < 8; ++i)
    {
        float3 uvw = LocalToUVW(origin);
        if (any(uvw < 0.0) || any(uvw >= 1.0)) return origin;
        int3 cell = (int3)floor(uvw * VOLUME_GRID_SIZE);
        if (GiAlbedo.Load(int4(cell, 0)).a <= 0.5) return origin;

        float3 surfaceNormal = GiNormal.Load(int4(cell, 0)).rgb * 2.0 - 1.0;
        if (dot(surfaceNormal, normal) < -0.1) return nearOrigin;

        float3 boundary = -VOLUME_WORLD_EXTENT + ((float3)cell +
            float3(normal.x > 0.0, normal.y > 0.0, normal.z > 0.0)) * cellSize;
        float3 distance = float3(
            abs(normal.x) > 1e-8 ? (boundary.x - origin.x) / normal.x : 1e20,
            abs(normal.y) > 1e-8 ? (boundary.y - origin.y) / normal.y : 1e20,
            abs(normal.z) > 1e-8 ? (boundary.z - origin.z) / normal.z : 1e20);
        float advance = max(min(distance.x, min(distance.y, distance.z)), 0.0) + epsilon;
        if (travel + advance > budget) return nearOrigin;
        origin += normal * advance;
        travel += advance;
    }
    // No represented gap before an opposing surface or the search limit: keep
    // the original occlusion instead of teleporting through a wall to find light.
    return nearOrigin;
}

bool GiTraceVoxel(float3 origin, float3 direction, float start, float span, out int3 hit, float3 receiverNormal)
{
    hit = int3(0, 0, 0);
    if (span <= 0.0) return false;
    float3 ray = normalize(direction);
    float3 inverse = float3(abs(ray.x) > 1e-8 ? 1.0 / ray.x : 1e20,
                            abs(ray.y) > 1e-8 ? 1.0 / ray.y : 1e20,
                            abs(ray.z) > 1e-8 ? 1.0 / ray.z : 1e20);
    float3 a = (-VOLUME_WORLD_EXTENT - origin) * inverse;
    float3 b = ( VOLUME_WORLD_EXTENT - origin) * inverse;
    float3 lo = min(a, b), hi = max(a, b);
    float enter = max(start, max(lo.x, max(lo.y, lo.z)));
    float leave = min(start + span, min(hi.x, min(hi.y, hi.z)));
    if (leave <= enter) return false;

    float cellSize = GiVoxelSize();
    float travel = enter;
    int3 cell = clamp((int3)floor(LocalToUVW(origin + ray * travel) * VOLUME_GRID_SIZE), 0, VOLUME_GRID_SIZE - 1);
    int3 step = int3(ray.x >= 0.0 ? 1 : -1, ray.y >= 0.0 ? 1 : -1, ray.z >= 0.0 ? 1 : -1);
    float3 boundary = -VOLUME_WORLD_EXTENT + ((float3)cell + float3(step.x > 0, step.y > 0, step.z > 0)) * cellSize;
    float3 next = (boundary - origin) * inverse;
    float3 delta = abs(inverse) * cellSize;
    next = float3(abs(ray.x) > 1e-8 ? next.x : 1e20, abs(ray.y) > 1e-8 ? next.y : 1e20, abs(ray.z) > 1e-8 ? next.z : 1e20);

    float receiverLength2 = dot(receiverNormal, receiverNormal);
    bool receiverPrefix = start <= 0.0 && enter <= 0.0 && receiverLength2 > 1e-12 && all(isfinite(receiverNormal));
    receiverNormal *= rsqrt(max(receiverLength2, 1e-12));
    receiverPrefix = receiverPrefix && dot(receiverNormal, ray) > 0.0;
    float receiverLimit = cellSize * GI_SURFACE_SEARCH_VOXELS;

    // A ray can cross at most 3*N voxel boundaries in an N^3 volume.
    [loop] for (uint i = 0; i < 3 * VOLUME_GRID_SIZE + 3; ++i)
    {
        if (travel >= leave || any(cell < 0) || any(cell >= VOLUME_GRID_SIZE)) break;
        float crossing = min(next.x, min(next.y, next.z));
        float cellEnd = min(leave, crossing);
        if (cellEnd > travel)
        {
            if (GiAlbedo.Load(int4(cell, 0)).a > 0.5)
            {
                bool selfSurface = false;
                if (receiverPrefix && i < 8 && cellEnd <= receiverLimit)
                {
                    float3 n = GiNormal.Load(int4(cell, 0)).rgb * 2.0 - 1.0;
                    n *= rsqrt(max(dot(n, n), 1e-12));
                    selfSurface = dot(n, receiverNormal) >= 0.9 && dot(n, ray) > 0.0;
                }
                // A real surface receiver can occupy its own coarse shell. Only
                // its contiguous, outward-facing prefix is exempt; other geometry
                // and every ordinary world-probe ray remain opaque.
                if (!selfSurface)
                {
                    hit = cell;
                    return true;
                }
            }
            else
            {
                // Never restart self exclusion at a later wall or interval.
                receiverPrefix = false;
            }
        }
        bool3 advance = next <= crossing;
        cell += int3(advance.x ? step.x : 0, advance.y ? step.y : 0, advance.z ? step.z : 0);
        next += float3(advance.x ? delta.x : 0.0, advance.y ? delta.y : 0.0, advance.z ? delta.z : 0.0);
        travel = crossing;
    }
    return false;
}

bool GiTraceVoxel(float3 origin, float3 direction, float start, float span, out int3 hit)
{
    return GiTraceVoxel(origin, direction, start, span, hit, float3(0, 0, 0));
}

float GiSegmentVisibility(float3 localStart, float3 localEnd, float3 receiverNormal)
{
    float3 delta = localEnd - localStart;
    float distance = length(delta);
    if (distance < GiVoxelSize() * 0.002) return 1.0;
    int3 hit;
    return GiTraceVoxel(localStart, delta / distance, 0.0, max(distance - GiVoxelSize() * 0.002, 0.0), hit, receiverNormal) ? 0.0 : 1.0;
}

float GiSegmentVisibility(float3 localStart, float3 localEnd)
{
    return GiSegmentVisibility(localStart, localEnd, float3(0, 0, 0));
}

float4 GiTraceSurface(float3 origin, float3 direction, float start, float span, float3 receiverNormal)
{
    int3 hit;
    if (!GiTraceVoxel(origin, direction, start, span, hit, receiverNormal)) return float4(0, 0, 0, 1);
    float3 n = normalize(GiNormal.Load(int4(hit, 0)).rgb * 2.0 - 1.0);
    // A single-sided diffuse surface cannot emit its front-side light through its back.
    float3 outgoing = dot(n, -direction) > 0.0 ? GiRadianceSmooth.Load(int4(hit, 0)).rgb : float3(0, 0, 0);
    return float4(max(outgoing, 0.0), 0.0);
}

float4 GiTraceSurface(float3 origin, float3 direction, float start, float span)
{
    return GiTraceSurface(origin, direction, start, span, float3(0, 0, 0));
}

float GiInjectionSunVisibility(float3 local, float3 normal, float3 toLight)
{
    // Only the GI light injection checks visibility here. Direct material
    // lighting has no shadow map or shadow pass.
    int3 hit;
    return GiTraceVoxel(GiSurfaceOrigin(local, normal), toLight, 0.0,
                        4.0 * VOLUME_WORLD_EXTENT, hit, normal) ? 0.0 : 1.0;
}

int2 GiEnvironmentFoldTexel(int2 texel)
{
    // Bilinear taps can enter the one-texel halo. Crossing an octahedral edge
    // reflects across that edge and reverses the coordinate along it.
    int size = (int)GI_ENV_SIZE;
    if (texel.x < 0 || texel.x >= size)
    {
        texel.x = texel.x < 0 ? -texel.x - 1 : 2 * size - texel.x - 1;
        texel.y = size - texel.y - 1;
    }
    if (texel.y < 0 || texel.y >= size)
    {
        texel.y = texel.y < 0 ? -texel.y - 1 : 2 * size - texel.y - 1;
        texel.x = size - texel.x - 1;
    }
    return texel;
}

float3 GiEnvironmentBilinear(float2 uv, uint layer)
{
    float2 grid = uv * (float)GI_ENV_SIZE - 0.5;
    int2 base = (int2)floor(grid);
    float2 fraction = frac(grid);
    float3 a = GiEnvironment.Load(int4(GiEnvironmentFoldTexel(base), layer, 0)).rgb;
    float3 b = GiEnvironment.Load(int4(GiEnvironmentFoldTexel(base + int2(1, 0)), layer, 0)).rgb;
    float3 c = GiEnvironment.Load(int4(GiEnvironmentFoldTexel(base + int2(0, 1)), layer, 0)).rgb;
    float3 d = GiEnvironment.Load(int4(GiEnvironmentFoldTexel(base + int2(1, 1)), layer, 0)).rgb;
    return lerp(lerp(a, b, fraction.x), lerp(c, d, fraction.x), fraction.y);
}

float3 GiEnvironmentLayer(float3 direction, float layer)
{
    float2 uv = OctEncode(normalize(direction));
    float clampedLayer = clamp(layer, 0.0, (float)(GI_ENV_LAYERS - 1));
    uint lower = (uint)floor(clampedLayer);
    uint upper = min(lower + 1, (uint)GI_ENV_LAYERS - 1);
    float fraction = frac(clampedLayer);
    float3 low = GiEnvironmentBilinear(uv, lower);
    if (fraction <= 0.0) return low;
    return lerp(low, GiEnvironmentBilinear(uv, upper), fraction);
}

float3 GiDiffuseSky(frame_globals globals, float3 normal)
{
    return GiEnvironmentLayer(normal, 0.0) * LIGHT_SKY_STRENGTH;
}

float3 GiSpecularSky(frame_globals globals, float3 reflection, float roughness)
{
    return GiEnvironmentLayer(reflection, 1.0 + saturate(roughness) * (GI_ENV_SPECULAR_LEVELS - 1)) * LIGHT_SKY_STRENGTH;
}

float3 GiSampleDiffuseProbes(float3 local, float3 normal, float3 geometricNormal, out float skyVisibility)
{
    float3 origin = GiSurfaceOrigin(local, geometricNormal);
    float3 uvw = LocalToUVW(origin);
    if (any(uvw < 0.0) || any(uvw > 1.0))
    {
        skyVisibility = 1.0;
        return float3(0, 0, 0);
    }
    float3 grid = uvw * RC_IRRADIANCE_SIZE - 0.5;
    int3 base = (int3)floor(grid);
    float3 fraction = frac(grid);
    float3 light = float3(0, 0, 0);
    float sky = 0.0, total = 0.0;
    [unroll] for (uint corner = 0; corner < 8; ++corner)
    {
        int3 offset = int3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        int3 probe = clamp(base + offset, 0, (int)RC_IRRADIANCE_SIZE - 1);
        float3 p = RcProbeLocal((uint3)probe, RC_IRRADIANCE_SIZE);
        int3 probeCell = clamp((int3)floor(LocalToUVW(p) * VOLUME_GRID_SIZE), 0, VOLUME_GRID_SIZE - 1);
        if (GiAlbedo.Load(int4(probeCell, 0)).a > 0.5) continue;
        float3 w = lerp(1.0 - fraction, fraction, (float3)offset);
        float weight = w.x * w.y * w.z;
        weight *= saturate(dot(p - local, geometricNormal) / GiVoxelSize());
        if (weight <= 1e-4) continue;
        weight *= GiSegmentVisibility(origin, p, geometricNormal);
        if (weight <= 0.0) continue;
        float4 value = float4(0, 0, 0, 0);
        float axisWeight = 0.0;
        [unroll] for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
        {
            float aligned = max(dot(normal, LightAxis[axis]), 0.0);
            value += GiIrradiance(axis).Load(int4(probe, 0)) * aligned;
            axisWeight += aligned;
        }
        value /= max(axisWeight, 1e-4);
        light += value.rgb * weight;
        sky += value.a * weight;
        total += weight;
    }
    if (total > 1e-4)
    {
        skyVisibility = saturate(sky / total);
        return light / total * LIGHT_GI_STRENGTH;
    }

    // No visible probe: trace a small cosine-weighted hemisphere instead of
    // restoring rejected weights or assuming that a closed room sees the sky.
    float3 tangent, bitangent;
    GiDirectionBasis(normal, tangent, bitangent);
    light = 0.0;
    sky = 0.0;
    [loop] for (uint sample = 0; sample < 8; ++sample)
    {
        float2 xi = GiHammersley(sample, 8);
        float radius = sqrt((sample + 0.5) / 8.0);
        float phi = 6.28318530718 * xi.y;
        float3 direction = tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) + normal * sqrt(1.0 - radius * radius);
        float4 ray = GiTraceSurface(origin, direction, 0.0, 4.0 * VOLUME_WORLD_EXTENT, geometricNormal);
        light += ray.rgb;
        sky += ray.a;
    }
    skyVisibility = sky * 0.125;
    return light * (0.125 * LIGHT_GI_STRENGTH);
}

float3 GiSampleDiffuseProbes(float3 local, float3 normal, out float skyVisibility)
{
    return GiSampleDiffuseProbes(local, normal, normal, skyVisibility);
}

#endif
