#include "ShaderInterop.h"

// Built only when the source environment changes. Layer 0 stores E/pi;
// the remaining layers convolve radiance with progressively rougher GGX lobes.
[numthreads(8, 8, 1)]
void Prefilter(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GI_ENV_SIZE || id.y >= GI_ENV_SIZE || id.z >= GI_ENV_LAYERS) return;
    frame_globals globals = LoadGlobals(pc.GlobalsPtr);
    uint sky = min(globals.SkyCubemap, (uint)MAX_CUBEMAPS - 1);
    float3 n = RcDirection(id.xy, GI_ENV_SIZE);
    float roughness = id.z > 0 ? (float)(id.z - 1) / (float)(GI_ENV_SPECULAR_LEVELS - 1) : 1.0;
    float3 sum = 0.0;
    float total = 0.0;
    float sourceSize = exp2(max((float)globals.SkyMipCount - 1.0, 0.0));
    float texelAngle = 12.56637061436 / (6.0 * sourceSize * sourceSize);
    float3 tangent, bitangent;
    GiDirectionBasis(n, tangent, bitangent);
    [loop] for (uint sample = 0; sample < GI_ENV_SAMPLES; ++sample)
    {
        float2 xi = GiHammersley(sample, GI_ENV_SAMPLES);
        xi.x = (sample + 0.5) / (float)GI_ENV_SAMPLES;
        float3 direction;
        float weight = 1.0, pdf;
        if (id.z == 0)
        {
            float radius = sqrt(xi.x);
            float phi = 6.28318530718 * xi.y;
            direction = tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) + n * sqrt(1.0 - xi.x);
            pdf = max(dot(n, direction), 0.0) * RC_INV_PI;
        }
        else
        {
            float3 h = GiSampleGGX(xi, roughness, n);
            direction = roughness == 0.0 ? n : reflect(-n, h);
            weight = max(dot(n, direction), 0.0);
            float ndoth = max(dot(n, h), 0.0);
            float alpha = max(roughness * roughness, 0.001);
            float d = ndoth * ndoth * (alpha * alpha - 1.0) + 1.0;
            pdf = (alpha * alpha * RC_INV_PI / max(d * d, 1e-8)) * 0.25;
        }
        float lod = id.z == 1 ? 0.0 : max(0.5 * log2(1.0 / max(GI_ENV_SAMPLES * pdf * texelAngle, 1e-8)), 0.0);
        sum += Sky[sky].SampleLevel(Samp, direction, lod).rgb * weight;
        total += weight;
    }
    GiEnvironmentRW[id] = float4(sum / max(total, 1e-4), 1.0);
}
