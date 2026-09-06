#include "Compute.hlsl"

static const float VOXEL_SAMPLE_DENSITY = 1.5;
static const uint  VOXEL_MAX_STEPS      = 512;

float3 WorldToVoxel(float3 world, float3 center, float extent, uint gridSize)
{
    float3 local = (world - center) / (2.0 * extent) + 0.5;

    return local * (float)gridSize;
}

uint PackColorKey(float3 color)
{
    uint3 quantized = (uint3)(saturate(color) * 255.0 + 0.5);

    return 0x80000000u | (quantized.x << 16) | (quantized.y << 8) | quantized.z;
}

float3 UnpackColorKey(uint key)
{
    uint3 quantized = uint3((key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF);

    return (float3)quantized / 255.0;
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Clear(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= VOLUME_GRID_SIZE))
    {
        return;
    }

    UintVolumesRW[UINT_SLOT_ALBEDO][id] = 0;
    UintVolumesRW[UINT_SLOT_NORMAL][id] = 0;
}

[numthreads(VOXEL_GROUP_SIZE, 1, 1)]
void Mesh(uint3 id : SV_DispatchThreadID)
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

    uint albedoKey = PackColorKey(material.BaseColor.rgb);
    uint normalKey = PackColorKey(normal * 0.5 + 0.5);

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
                InterlockedMax(UintVolumesRW[UINT_SLOT_ALBEDO][uint3(coord)], albedoKey);
                InterlockedMax(UintVolumesRW[UINT_SLOT_NORMAL][uint3(coord)], normalKey);
            }
        }
    }
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Resolve(uint3 id : SV_DispatchThreadID)
{
    volume_op_params params = LoadVolumeOpParams(pc.ParamsPtr);

    if (any(id >= params.Size))
    {
        return;
    }

    uint albedoKey = UintVolumesRW[UINT_SLOT_ALBEDO][id];
    uint normalKey = UintVolumesRW[UINT_SLOT_NORMAL][id];

    float occupancy = albedoKey ? 1.0 : 0.0;

    VolumesRW[params.SrcSlot][id] = float4(UnpackColorKey(albedoKey), occupancy);
    VolumesRW[params.DstSlot][id] = float4(UnpackColorKey(normalKey), occupancy);
}
