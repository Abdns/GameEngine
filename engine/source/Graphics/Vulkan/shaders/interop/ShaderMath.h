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

    float3 RcProbeLocal(uint3 probe, uint probeSize)
    {
        return (((float3)probe + 0.5) / (float)probeSize - 0.5) * (2.0 * VOLUME_WORLD_EXTENT);
    }
#endif

#endif
