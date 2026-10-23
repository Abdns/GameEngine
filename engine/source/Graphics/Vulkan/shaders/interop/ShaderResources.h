#ifndef SHADERRESOURCES_H
#define SHADERRESOURCES_H

#include "ShaderData.h"

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

#ifndef __cplusplus

[[vk::binding(BINDING_TEXTURES, SET_GLOBAL)]] Texture2D    Tex[TEXTURE_HEAP_SIZE];
[[vk::binding(BINDING_SAMPLER, SET_GLOBAL)]]  SamplerState Samp;
[[vk::binding(BINDING_CUBEMAPS, SET_GLOBAL)]] TextureCube  Sky[MAX_CUBEMAPS];

[[vk::push_constant]] push_constants pc;

#define LoadPassParams(Type) vk::BufferPointer<Type, 16>(pc.ParamsPtr).Get()

vertex LoadVertex(uint64_t base, uint index)
{
    return vk::RawBufferLoad<vertex>(base + (uint64_t)index * VERTEX_STRIDE, 4);
}

gpu_material LoadMaterial(uint64_t base, uint slot)
{
    return vk::RawBufferLoad<gpu_material>(base + (uint64_t)slot * MATERIAL_STRIDE, 16);
}

uint LoadIndex(uint64_t base, uint index)
{
    return vk::RawBufferLoad<uint>(base + (uint64_t)index * 4, 4);
}

rect_params LoadRect(uint64_t base, uint index)
{
    return vk::BufferPointer<rect_params, 16>(base + (uint64_t)index * RECT_PARAMS_STRIDE).Get();
}

frame_globals LoadGlobals(uint64_t address)
{
    return vk::BufferPointer<frame_globals, 16>(address).Get();
}

#endif
#endif