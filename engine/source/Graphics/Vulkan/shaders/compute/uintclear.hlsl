#include "ShaderInterop.h"

[[vk::image_format("r32ui")]]
[[vk::binding(BINDING_UINT_VOLUMES, SET_GLOBAL)]] RWTexture3D<uint> UintVolumesRW[MAX_UINT_VOLUMES];

[[vk::push_constant]] push_constants pc;

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    volume_params params = LoadVolumeParams(pc.ParamsPtr);

    if (any(id >= params.VolumeSize))
    {
        return;
    }

    UintVolumesRW[params.VolumeSlot][id] = 0;
}
