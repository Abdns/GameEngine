#ifndef COMPUTE_HLSL
#define COMPUTE_HLSL

#include "ShaderInterop.h"
#include "../interop/GiSampling.h"

struct probe_gather
{
    uint3 Corner[8];
    float Weight[8];
    float Total;
};

struct axis_light
{
    float3 Total[LIGHT_DIRECTIONS];
    float  Sky[LIGHT_DIRECTIONS];
    float  Weight[LIGHT_DIRECTIONS];
};

float VolumeTraceDistance()
{
    // The longest segment inside the cubic GI domain is its diagonal.
    return 2.0 * VOLUME_WORLD_EXTENT * 1.73205081;
}

float ProbeOccupancy(uint3 probe, uint probeSize)
{
    float3 uvw = LocalToUVW(RcProbeLocal(probe, probeSize));
    int3 voxel = clamp((int3)(uvw * VOLUME_GRID_SIZE), 0, VOLUME_GRID_SIZE - 1);

    return GiAlbedo.Load(int4(voxel, 0)).a;
}

probe_gather ProbeCorners(float3 grid, uint probeSize)
{
    int3   base     = (int3)floor(grid);
    float3 fraction = saturate(grid - (float3)base);

    fraction = fraction * fraction * (3.0 - 2.0 * fraction);

    probe_gather gather;

    gather.Total = 0.0;

    for (uint c = 0; c < 8; ++c)
    {
        int3 offset = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);

        float3 axisWeight = lerp(1.0 - fraction, fraction, (float3)offset);

        int3 probe = clamp(base + offset, int3(0, 0, 0), (int3)(probeSize - 1));

        gather.Corner[c] = (uint3)probe;
        gather.Weight[c] = axisWeight.x * axisWeight.y * axisWeight.z;
        gather.Total    += gather.Weight[c];
    }

    return gather;
}

void WeightProbes(inout probe_gather gather, float scale[8])
{
    float total = 0.0;

    for (uint c = 0; c < 8; ++c)
    {
        total += gather.Weight[c] * scale[c];
    }

    for (uint w = 0; w < 8; ++w)
    {
        gather.Weight[w] *= scale[w];
    }

    gather.Total = total;
}

float ProbeNorm(probe_gather gather)
{
    return 1.0 / max(gather.Total, 1e-4);
}

axis_light AxisLightZero()
{
    axis_light light;

    for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
    {
        light.Total[axis]  = float3(0.0, 0.0, 0.0);
        light.Sky[axis]    = 0.0;
        light.Weight[axis] = 0.0;
    }

    return light;
}

void AxisAccumulate(inout axis_light light, float3 direction, float3 radiance, float sky, float scale)
{
    for (uint axis = 0; axis < LIGHT_DIRECTIONS; ++axis)
    {
        float weight = max(dot(LightAxis[axis], direction), 0.0) * scale;

        light.Total[axis]  += radiance * weight;
        light.Sky[axis]    += sky * weight;
        light.Weight[axis] += weight;
    }
}

float4 AxisResolve(axis_light light, uint axis)
{
    float norm = 1.0 / max(light.Weight[axis], 1e-4);

    // Normalized cosine/solid-angle quadrature approximates E/pi and preserves
    // constant radiance exactly, including at the lower directional resolutions.
    return float4(light.Total[axis] * norm, saturate(light.Sky[axis] * norm));
}

#endif
