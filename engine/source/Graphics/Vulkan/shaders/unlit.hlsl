#include "ShaderInterop.h"

struct vs_output
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 Color : COLOR0;
    [[vk::location(1)]] float2 UV    : TEXCOORD0;
};

vs_output VSMain(uint vertexID : SV_VertexID)
{
    draw_params params = LoadDrawParams(pc.ParamsPtr);

    vertex v = LoadVertex(params.Vertices, vertexID);

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    vs_output output;
    output.Position = mul(globals.ViewProj, mul(params.Model, float4(v.Position, 1.0)));
    output.Color    = v.Color;
    output.UV       = v.UV;
    return output;
}

float4 PSMain(vs_output input) : SV_Target
{
    draw_params params = LoadDrawParams(pc.ParamsPtr);

    gpu_material Material = LoadMaterial(params.Materials, params.MaterialSlot);

    return Tex[Material.TextureSlot].Sample(Samp, input.UV) * Material.BaseColor * params.Tint;
}
