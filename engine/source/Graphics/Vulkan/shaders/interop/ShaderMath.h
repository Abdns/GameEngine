#ifndef SHADERMATH_H
#define SHADERMATH_H

#ifndef __cplusplus

    float3 TransformNormal(float4x4 model, float3 normal)
    {
        float3x3 m = (float3x3)model;
        float3x3 cofactors = float3x3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
        float determinant = dot(m[0], cofactors[0]);
        float3 transformed = mul(cofactors, normal) * (determinant < 0.0 ? -1.0 : 1.0);
        return transformed / sqrt(max(dot(transformed, transformed), 1e-12));
    }

#endif

#endif
