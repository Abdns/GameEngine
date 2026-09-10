#include "interop/ShaderHeap.h"

static const float2 Corners[6] =
{
    float2(0.0, 0.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 0.0),
    float2(1.0, 1.0),
    float2(0.0, 1.0),
};

struct vs_output
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint Index : TEXCOORD1;
};

vs_output VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    rect_params params = LoadRect(pc.ParamsPtr, instanceID);

    float2 Corner = Corners[vertexID];

    vs_output output;
    output.Position = float4(lerp(params.Rect.xy, params.Rect.zw, Corner), 0.0, 1.0);
    output.UV       = lerp(params.UVRect.xy, params.UVRect.zw, Corner);
    output.Index    = instanceID;
    return output;
}

float4 PSMain(vs_output input) : SV_Target
{
    rect_params params = LoadRect(pc.ParamsPtr, input.Index);

    float4 Color = params.Tint;

    if (params.TextureSlot != TEXTURE_NONE)
    {
        Color *= Tex[params.TextureSlot].Sample(Samp, input.UV);
    }

    return Color;
}
