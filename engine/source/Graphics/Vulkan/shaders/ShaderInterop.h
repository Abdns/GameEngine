#ifndef SHADERINTEROP_H
#define SHADERINTEROP_H

#include "interop/ShaderVolumes.h"
#include "interop/ShaderHeap.h"

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

#ifndef __cplusplus

[[vk::binding(BINDING_TEXTURES, SET_GLOBAL)]] Texture2D    Tex[TEXTURE_HEAP_SIZE];
[[vk::binding(BINDING_SAMPLER,  SET_GLOBAL)]] SamplerState Samp;
[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];

[[vk::binding(BINDING_VOLUMES, SET_GLOBAL)]] Texture3D Volumes[MAX_VOLUMES];

[[vk::image_format("rgba16f")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::image_format("r32ui")]]
[[vk::binding(BINDING_UINT_VOLUMES, SET_GLOBAL)]] RWTexture3D<uint> UintVolumesRW[MAX_UINT_VOLUMES];

[[vk::binding(BINDING_VOLUME_SAMPLER, SET_GLOBAL)]] SamplerState VolumeSamp;

[[vk::push_constant]] push_constants pc;

#endif

#endif
