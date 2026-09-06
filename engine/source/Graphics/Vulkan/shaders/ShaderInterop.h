#ifndef SHADERINTEROP_H
#define SHADERINTEROP_H

#include "interop/ShaderHeap.h"
#include "interop/ShaderVolumes.h"

#ifdef __cplusplus
#define float4x4 Matrix4
#define float4   Vector4
#define float3   Vector3
#define float2   Vector2
#define uint     uint32
#define gpu_ptr  uint64
#else
#define gpu_ptr  uint64_t
#endif

#include "interop/ShaderParams.h"
#include "interop/ShaderMath.h"

#ifdef __cplusplus
    #undef float4x4
    #undef float4
    #undef float3
    #undef float2
    #undef uint
    #undef gpu_ptr
#endif

#endif
