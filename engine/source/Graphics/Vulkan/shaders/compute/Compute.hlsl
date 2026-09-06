#ifndef COMPUTE_HLSL
#define COMPUTE_HLSL

#include "ShaderInterop.h"

[[vk::binding(BINDING_TEXTURES, SET_GLOBAL)]] Texture2D    Tex[TEXTURE_HEAP_SIZE];
[[vk::binding(BINDING_SAMPLER,  SET_GLOBAL)]] SamplerState Samp;
[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D Volumes[MAX_VOLUMES];

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::image_format("r32ui")]]
[[vk::binding(BINDING_UINT_VOLUMES, SET_GLOBAL)]] RWTexture3D<uint> UintVolumesRW[MAX_UINT_VOLUMES];

[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

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

float4 TraceVolume(uint slot, float3 origin, float3 direction, float start, float span, uint steps)
{
    float stepSize = span / (float)steps;

    float3 radiance      = float3(0.0, 0.0, 0.0);
    float  transmittance = 1.0;

    for (uint i = 0; i < steps; ++i)
    {
        float travel = start + stepSize * ((float)i + 0.5);

        float3 uvw = LocalToUVW(origin + direction * travel);

        if (any(uvw < 0.0) || any(uvw > 1.0))
        {
            break;
        }

        float4 voxel = Volumes[slot].SampleLevel(VolumeSamp, SmoothUVW(uvw, (float)LIGHT_GRID_SIZE), 0);

        if (voxel.a > 0.001)
        {
            float absorbed = 1.0 - exp(-voxel.a * RC_EXTINCTION * stepSize);

            radiance      += transmittance * (voxel.rgb / voxel.a) * absorbed;
            transmittance *= 1.0 - absorbed;

            if (transmittance < 0.005)
            {
                transmittance = 0.0;
                break;
            }
        }
    }

    return float4(radiance, transmittance);
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

    if (total < 1e-4)
    {
        return;
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

    return float4(light.Total[axis] * norm, saturate(light.Sky[axis] * norm));
}

#endif
