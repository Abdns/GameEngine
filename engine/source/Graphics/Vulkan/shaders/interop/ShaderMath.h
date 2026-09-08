#ifndef SHADERMATH_H
#define SHADERMATH_H

#ifndef __cplusplus

    static const float3 LightAxis[LIGHT_DIRECTIONS] =
    {
        float3( 1.0,  0.0,  0.0),
        float3(-1.0,  0.0,  0.0),
        float3( 0.0,  1.0,  0.0),
        float3( 0.0, -1.0,  0.0),
        float3( 0.0,  0.0,  1.0),
        float3( 0.0,  0.0, -1.0),
    };

    float3 LocalToUVW(float3 local)
    {
        return (local + VOLUME_WORLD_EXTENT) / (2.0 * VOLUME_WORLD_EXTENT);
    }

    float3 SmoothUVW(float3 uvw, float size)
    {
        float3 coord = uvw * size - 0.5;
        float3 anchor = floor(coord);
        float3 fraction = coord - anchor;

        fraction = fraction * fraction * (3.0 - 2.0 * fraction);

        return (anchor + fraction + 0.5) / size;
    }

    float3 OctDecode(float2 f)
    {
        float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));

        float t = saturate(-n.z);

        n.x += n.x >= 0.0 ? -t : t;
        n.y += n.y >= 0.0 ? -t : t;

        return normalize(n);
    }

    float3 RcDirection(uint2 dirUV, uint dirRes)
    {
        float2 f = ((float2)dirUV + 0.5) / (float)dirRes * 2.0 - 1.0;

        return OctDecode(f);
    }

    float2 OctEncode(float3 n)
    {
        n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6);
        float2 f = n.xy;
        if (n.z < 0.0)
            f = (1.0 - abs(f.yx)) * float2(f.x >= 0.0 ? 1.0 : -1.0, f.y >= 0.0 ? 1.0 : -1.0);
        return f * 0.5 + 0.5;
    }

    float SphericalTriangleArea(float3 a, float3 b, float3 c)
    {
        return 2.0 * atan2(abs(dot(a, cross(b, c))), max(1.0 + dot(a, b) + dot(b, c) + dot(c, a), 1e-8));
    }

    // Octahedral texels do not cover equal solid angles.
    float RcDirectionSolidAngle(uint2 uv, uint resolution)
    {
        float2 lo = (float2)uv / (float)resolution * 2.0 - 1.0;
        float2 hi = (float2)(uv + 1) / (float)resolution * 2.0 - 1.0;
        float3 a = OctDecode(lo);
        float3 b = OctDecode(float2(hi.x, lo.y));
        float3 c = OctDecode(hi);
        float3 d = OctDecode(float2(lo.x, hi.y));
        return SphericalTriangleArea(a, b, c) + SphericalTriangleArea(a, c, d);
    }

    float3 TransformNormal(float4x4 model, float3 normal)
    {
        float3x3 m = (float3x3)model;
        float3x3 cofactors = float3x3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
        float determinant = dot(m[0], cofactors[0]);
        float3 transformed = mul(cofactors, normal) * (determinant < 0.0 ? -1.0 : 1.0);
        return transformed / sqrt(max(dot(transformed, transformed), 1e-12));
    }

    void GiDirectionBasis(float3 n, out float3 tangent, out float3 bitangent)
    {
        tangent = normalize(cross(abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0), n));
        bitangent = cross(n, tangent);
    }

    float2 GiHammersley(uint index, uint count)
    {
        return float2((float)index / (float)count, (float)reversebits(index) * 2.3283064365386963e-10);
    }

    float3 GiSampleGGX(float2 xi, float roughness, float3 n)
    {
        float alpha = max(roughness * roughness, 0.001);
        float phi = 6.28318530718 * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        float3 tangent, bitangent;
        GiDirectionBasis(n, tangent, bitangent);
        return tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + n * cosTheta;
    }

    float3 RcProbeLocal(uint3 probe, uint probeSize)
    {
        return (((float3)probe + 0.5) / (float)probeSize - 0.5) * (2.0 * VOLUME_WORLD_EXTENT);
    }

    void RcCascadeInterval(uint cascade, out float start, out float span)
    {
        float base  = (2.0 * VOLUME_WORLD_EXTENT) / (float)RC_PROBE_SIZE;
        float scale = 1.0;
        float begin = 0.0;

        for (uint i = 0; i < cascade; ++i)
        {
            begin += base * scale;
            scale *= RC_INTERVAL_SCALE;
        }

        start = begin;
        span  = base * scale;
    }
#endif

#endif
