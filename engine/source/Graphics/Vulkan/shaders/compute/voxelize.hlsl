#include "ShaderInterop.h"

[[vk::image_format("rgba8")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

static const float VOXEL_SAMPLE_DENSITY = 1.5;
static const uint  VOXEL_MAX_STEPS      = 512;

float3 WorldToVoxel(float3 world, float3 center, float extent, uint gridSize)
{
    float3 local = (world - center) / (2.0 * extent) + 0.5;

    return local * (float)gridSize;
}

[numthreads(VOXEL_GROUP_SIZE, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    voxelize_params params = LoadVoxelizeParams(pc.ParamsPtr);

    if (id.x >= params.TriangleCount)
    {
        return;
    }

    uint base = params.FirstIndex + id.x * 3;

    uint i0 = LoadIndex(params.Indices, base + 0) + params.FirstVertex;
    uint i1 = LoadIndex(params.Indices, base + 1) + params.FirstVertex;
    uint i2 = LoadIndex(params.Indices, base + 2) + params.FirstVertex;

    vertex a = LoadVertex(params.Vertices, i0);
    vertex b = LoadVertex(params.Vertices, i1);
    vertex c = LoadVertex(params.Vertices, i2);

    float3 w0 = mul(params.Model, float4(a.Position, 1.0)).xyz;
    float3 w1 = mul(params.Model, float4(b.Position, 1.0)).xyz;
    float3 w2 = mul(params.Model, float4(c.Position, 1.0)).xyz;

    float3 normal = normalize(mul((float3x3)params.Model, a.Normal + b.Normal + c.Normal));

    float3 v0 = WorldToVoxel(w0, params.GridCenter, params.GridExtent, params.GridSize);
    float3 v1 = WorldToVoxel(w1, params.GridCenter, params.GridExtent, params.GridSize);
    float3 v2 = WorldToVoxel(w2, params.GridCenter, params.GridExtent, params.GridSize);

    float longest = max(length(v1 - v0), max(length(v2 - v0), length(v2 - v1)));

    uint steps = (uint)clamp(ceil(longest * VOXEL_SAMPLE_DENSITY), 1.0, (float)VOXEL_MAX_STEPS);

    gpu_material material = LoadMaterial(params.Materials, params.MaterialSlot);

    float4 storedAlbedo = float4(material.BaseColor.rgb, 1.0);
    float4 storedNormal = float4(normal * 0.5 + 0.5, 1.0);

    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;

    for (uint i = 0; i <= steps; ++i)
    {
        float alpha = (float)i / (float)steps;

        for (uint j = 0; j <= steps - i; ++j)
        {
            float beta = (float)j / (float)steps;

            int3 coord = int3(floor(v0 + edge1 * alpha + edge2 * beta));

            if (all(coord >= 0) && all(coord < (int)params.GridSize))
            {
                VolumesRW[params.VolumeSlot][uint3(coord)]  = storedAlbedo;
                VolumesRW[params.NormalSlot][uint3(coord)]  = storedNormal;
            }
        }
    }
}
