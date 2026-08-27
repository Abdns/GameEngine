#include "ShaderInterop.h"

[[vk::image_format("rgba8")]]
[[vk::binding(BINDING_STORAGE_VOLUMES, SET_GLOBAL)]] RWTexture3D<float4> VolumesRW[MAX_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    volume_params params = LoadVolumeParams(pc.ParamsPtr);

    if (any(id >= params.VolumeSize))
    {
        return;
    }

    VolumesRW[params.VolumeSlot][id] = float4(0.0, 0.0, 0.0, 0.0);
}
