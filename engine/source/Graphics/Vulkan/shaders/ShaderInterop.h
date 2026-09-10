#ifndef SHADERINTEROP_H
#define SHADERINTEROP_H

#ifndef SHADERHEAP_H
#define SHADERHEAP_H

#define SET_GLOBAL 0

#define BINDING_TEXTURES 0
#define BINDING_SAMPLER  1
#define BINDING_CUBEMAPS 2
#define BINDING_COUNT    3

#define MAX_TEXTURES 32
#define MAX_CUBEMAPS 4

#define MAX_MATERIALS 64

#define TEXTURE_NONE 0xFFFFFFFF

#define TEXTURE_SLOT_SCENE MAX_TEXTURES
#define TEXTURE_SLOT_POST  (TEXTURE_SLOT_SCENE + 1)
#define TEXTURE_HEAP_SIZE (TEXTURE_SLOT_POST + 1)

#define VERTEX_STRIDE    44
#define MATERIAL_STRIDE  32
#define RECT_PARAMS_STRIDE 64

#endif


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

#include "interop/ShaderHeap.h"
#include "interop/ShaderParams.h"

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

[[vk::push_constant]] push_constants pc;

#endif

#endif
