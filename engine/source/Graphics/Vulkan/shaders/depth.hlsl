#include "ShaderInterop.h"

[[vk::push_constant]] push_constants pc;

struct vs_output
{
    float4 Position : SV_Position;
};

vs_output VSMain(uint vertexID : SV_VertexID)
{
    draw_params params = LoadDrawParams(pc.ParamsPtr);

    vertex v = LoadVertex(params.Vertices, vertexID);

    frame_globals globals = LoadGlobals(pc.GlobalsPtr);

    float4 worldPos = mul(params.Model, float4(v.Position, 1.0));

    vs_output output;
    output.Position = mul(globals.ViewProj, worldPos);
    return output;
}

void PSMain(vs_output input)
{
}
