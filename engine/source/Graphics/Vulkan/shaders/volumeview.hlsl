#include "ShaderInterop.h"

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D    Volumes[MAX_VOLUMES];
[[vk::binding(BINDING_SAMPLER,        SET_GLOBAL)]] SamplerState Samp;
[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

static const float2 Positions[3] =
{
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0),
};

static const float3 VolumeBackground = float3(0.02, 0.02, 0.03);

struct vs_output
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 UV        : TEXCOORD0;
    [[vk::location(1)]] float3 Direction : TEXCOORD1;
    [[vk::location(2)]] float3 Origin    : TEXCOORD2;
};

vs_output VSMain(uint vertexID : SV_VertexID)
{
    float2 NDC = Positions[vertexID];

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    vs_output output;
    output.Position  = float4(NDC, 0.0, 1.0);
    output.UV        = NDC * 0.5 + 0.5;
    output.Direction = globals.SkyRight.xyz * NDC.x + globals.SkyUp.xyz * NDC.y + globals.SkyForward.xyz;
    output.Origin    = globals.CameraPos;
    return output;
}

float4 SampleSlice(volume_params params, float2 uv)
{
    float3 UVW = float3(uv, params.VolumeSlice);

    return Volumes[params.VolumeSlot].SampleLevel(VolumeSamp, UVW, 0);
}

float4 SampleColumn(volume_params params, float2 uv)
{
    int size = (int)params.VolumeSize;

    int x = clamp((int)(uv.x * params.VolumeSize), 0, size - 1);
    int z = clamp((int)(uv.y * params.VolumeSize), 0, size - 1);

    for (int y = size - 1; y >= 0; --y)
    {
        float4 voxel = Volumes[params.VolumeSlot].Load(int4(x, y, z, 0));

        if (voxel.a > 0.0)
        {
            float shade = 0.35 + 0.65 * ((float)y / (float)size);

            return float4(voxel.rgb * shade, 1.0);
        }
    }

    return float4(VolumeBackground, 1.0);
}

float3 SafeDirection(float3 direction)
{
    float3 result = direction;

    result.x = abs(result.x) < 1e-6 ? 1e-6 : result.x;
    result.y = abs(result.y) < 1e-6 ? 1e-6 : result.y;
    result.z = abs(result.z) < 1e-6 ? 1e-6 : result.z;

    return result;
}

bool MarchVolume(volume_params params, float3 origin, float3 direction, out int3 hitCoord, out float4 hitVoxel, out float hitTravel, out float3 hitPosition)
{
    hitCoord    = int3(0, 0, 0);
    hitVoxel    = float4(0.0, 0.0, 0.0, 0.0);
    hitTravel   = 0.0;
    hitPosition = origin;

    float extent = VOLUME_WORLD_EXTENT;

    float3 ray = SafeDirection(normalize(direction));
    float3 inv = 1.0 / ray;

    float3 nearPlane = (-extent - origin) * inv;
    float3 farPlane  = ( extent - origin) * inv;

    float3 smaller = min(nearPlane, farPlane);
    float3 bigger  = max(nearPlane, farPlane);

    float enter = max(max(smaller.x, smaller.y), max(smaller.z, 0.0));
    float exit  = min(min(bigger.x, bigger.y), bigger.z);

    if (exit <= enter)
    {
        return false;
    }

    float voxelSize = (2.0 * extent) / (float)params.VolumeSize;
    float stepSize  = voxelSize * 0.5;

    int size = (int)params.VolumeSize;

    for (uint i = 0; i < VOLUME_MARCH_STEPS; ++i)
    {
        float travel = enter + stepSize * (float)i;

        if (travel > exit)
        {
            break;
        }

        float3 samplePos = origin + ray * travel;
        float3 local     = (samplePos + extent) / (2.0 * extent);

        int3 coord = clamp(int3(local * params.VolumeSize), 0, size - 1);

        float4 voxel = Volumes[params.VolumeSlot].Load(int4(coord, 0));

        if (voxel.a > 0.0)
        {
            hitCoord    = coord;
            hitVoxel    = voxel;
            hitTravel   = travel;
            hitPosition = samplePos;
            return true;
        }
    }

    return false;
}

float4 SampleCamera(volume_params params, float3 origin, float3 direction)
{
    int3   coord;
    float4 voxel;
    float  travel;
    float3 position;

    if (!MarchVolume(params, origin, direction, coord, voxel, travel, position))
    {
        return float4(VolumeBackground, 1.0);
    }

    float shade = 0.4 + 0.6 * saturate(1.0 - travel / (3.0 * VOLUME_WORLD_EXTENT));

    return float4(voxel.rgb * shade, 1.0);
}

float4 PSMain(vs_output input) : SV_Target
{
    volume_params params = LoadVolumeParams(pc.ParamsPtr);

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    float3 localOrigin = input.Origin - globals.VolumeCenter;

    if (params.VolumeMode == VOLUME_MODE_CAMERA)
    {
        return SampleCamera(params, localOrigin, input.Direction);
    }

    if (params.VolumeMode == VOLUME_MODE_COLUMN)
    {
        return SampleColumn(params, input.UV);
    }

    return SampleSlice(params, input.UV);
}
