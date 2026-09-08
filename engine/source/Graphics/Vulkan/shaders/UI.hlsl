#include "ShaderInterop.h"

struct vs_output
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

vs_output VSMain(uint vertexID : SV_VertexID)
{
    float2 NDC = float2((vertexID << 1) & 2, vertexID & 2) * 2.0 - 1.0;

    vs_output output;
    output.Position = float4(NDC, 0.0, 1.0);
    output.UV       = NDC * 0.5 + 0.5;
    return output;
}

float4 PSMain(vs_output input) : SV_Target
{
    image_params params = LoadPassParams(image_params);

    float3 Color = Tex[params.TextureSlot].Sample(Samp, input.UV).rgb;

    float2 FromCenter = abs(input.UV - 0.5);
    bool CrosshairV = (FromCenter.x < 0.0015 && FromCenter.y < 0.012);
    bool CrosshairH = (FromCenter.y < 0.0025 && FromCenter.x < 0.007);
    if (CrosshairV || CrosshairH)
    {
        Color = 1.0 - Color;
    }

    return float4(Color, 1.0);
}
