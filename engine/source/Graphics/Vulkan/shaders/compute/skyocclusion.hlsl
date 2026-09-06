#include "Compute.hlsl"

[numthreads(8, 1, 8)]
void Sweep(uint3 id : SV_DispatchThreadID)
{
    volume_op_params params = LoadVolumeOpParams(pc.ParamsPtr);

    if (id.x >= params.Size || id.z >= params.Size)
    {
        return;
    }

    float visibility = 1.0;

    for (int y = (int)params.Size - 1; y >= 0; --y)
    {
        uint3 coord = uint3(id.x, (uint)y, id.z);

        VolumesRW[params.DstSlot][coord] = float4(visibility, 0.0, 0.0, 1.0);

        visibility *= saturate(1.0 - VolumesRW[params.SrcSlot][coord].a);
    }
}

[numthreads(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]
void Blur(uint3 id : SV_DispatchThreadID)
{
    volume_op_params params = LoadVolumeOpParams(pc.ParamsPtr);

    if (any(id >= params.Size))
    {
        return;
    }

    int size = (int)params.Size;

    float total  = 0.0;
    float weight = 0.0;

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                int3 coord = clamp(int3(id) + int3(x, y, z), 0, size - 1);

                float tap = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0) * (z == 0 ? 2.0 : 1.0);

                total  += VolumesRW[params.SrcSlot][uint3(coord)].r * tap;
                weight += tap;
            }
        }
    }

    VolumesRW[params.DstSlot][id] = float4(total / weight, 0.0, 0.0, 1.0);
}
