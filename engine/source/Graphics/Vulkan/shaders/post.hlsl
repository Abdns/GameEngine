#include "ShaderInterop.h"

[[vk::binding(BINDING_TEXTURES, SET_GLOBAL)]] Texture2D    Tex[TEXTURE_HEAP_SIZE];
[[vk::binding(BINDING_SAMPLER,  SET_GLOBAL)]] SamplerState Samp;

[[vk::push_constant]] push_constants pc;

static const float2 Positions[3] =
{
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0),
};

static const float Exposure = 1.0;

float3 ACESFilm(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

struct vs_output
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

vs_output VSMain(uint vertexID : SV_VertexID)
{
    float2 NDC = Positions[vertexID];

    vs_output output;
    output.Position = float4(NDC, 0.0, 1.0);
    output.UV       = NDC * 0.5 + 0.5;
    return output;
}

float4 PSMain(vs_output input) : SV_Target
{
    image_params params = LoadImageParams(pc.ParamsPtr);

    float3 Color = Tex[params.TextureSlot].Sample(Samp, input.UV).rgb;

    Color = ACESFilm(Color * Exposure);

    return float4(Color, 1.0);
}
