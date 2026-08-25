#ifndef CUBEMAP_H
#define CUBEMAP_H

#include <math.h>

#include "Types.h"
#include "Memory.h"
#include "Half.h"
#include "HDR.h"

#define CUBEMAP_MAX_RADIANCE 50.0f

struct loaded_cubemap
{
    uint16 *Pixels;
    uint32  FaceSize;
    uint64  ByteSize;
};

internal void EquirectFaceDirection(uint32 Face, real32 U, real32 V, real32 *Out)
{
    switch (Face)
    {
        case 0: Out[0] =  1.0f; Out[1] =    -V; Out[2] =    -U; break;
        case 1: Out[0] = -1.0f; Out[1] =    -V; Out[2] =     U; break;
        case 2: Out[0] =     U; Out[1] =  1.0f; Out[2] =     V; break;
        case 3: Out[0] =     U; Out[1] = -1.0f; Out[2] =    -V; break;
        case 4: Out[0] =     U; Out[1] =    -V; Out[2] =  1.0f; break;
        default:Out[0] =    -U; Out[1] =    -V; Out[2] = -1.0f; break;
    }
}

internal void EquirectSample(loaded_hdr *Source, real32 S, real32 T, real32 *Out)
{
    real32 X = S * (real32)Source->Width  - 0.5f;
    real32 Y = T * (real32)Source->Height - 0.5f;

    int32 X0 = (int32)floorf(X);
    int32 Y0 = (int32)floorf(Y);

    real32 FX = X - (real32)X0;
    real32 FY = Y - (real32)Y0;

    for (uint32 Component = 0; Component < 3; ++Component)
    {
        real32 Accum = 0.0f;

        for (uint32 Corner = 0; Corner < 4; ++Corner)
        {
            int32 SX = X0 + (int32)(Corner & 1);
            int32 SY = Y0 + (int32)(Corner >> 1);

            while (SX < 0) SX += (int32)Source->Width;
            SX = SX % (int32)Source->Width;

            if (SY < 0) SY = 0;
            if (SY >= (int32)Source->Height) SY = (int32)Source->Height - 1;

            real32 Weight = ((Corner & 1) ? FX : (1.0f - FX)) * ((Corner >> 1) ? FY : (1.0f - FY));
            Accum += Weight * HalfToFloat(Source->Pixels[((memory_size)SY * Source->Width + SX) * 4 + Component]);
        }

        Out[Component] = Accum;
    }
}

internal loaded_cubemap EquirectToCubemap(memory_arena *Arena, loaded_hdr *Source, uint32 FaceSize)
{
    loaded_cubemap Result = {};

    Assert(Source->Pixels && FaceSize);

    uint32 FacesCount = 6;
    uint32 ChannelsPerPixel = 4;

    uint16 *Pixels = PushArray(Arena, (memory_size)FaceSize * FaceSize * FacesCount * ChannelsPerPixel, uint16);

    for (uint32 Face = 0; Face < FacesCount; ++Face)
    {
        uint16 *FacePixels = Pixels + (memory_size)Face * FaceSize * FaceSize * ChannelsPerPixel;

        for (uint32 Y = 0; Y < FaceSize; ++Y)
        {
            real32 V = 2.0f * (((real32)Y + 0.5f) / (real32)FaceSize) - 1.0f;

            for (uint32 X = 0; X < FaceSize; ++X)
            {
                real32 U = 2.0f * (((real32)X + 0.5f) / (real32)FaceSize) - 1.0f;

                real32 Dir[3];
                EquirectFaceDirection(Face, U, V, Dir);

                real32 Length = sqrtf(Dir[0] * Dir[0] + Dir[1] * Dir[1] + Dir[2] * Dir[2]);
                Dir[0] /= Length;
                Dir[1] /= Length;
                Dir[2] /= Length;

                real32 S = atan2f(Dir[2], Dir[0]) / (2.0f * Pi32) + 0.5f;
                real32 T = 0.5f - asinf(Dir[1]) / Pi32;

                real32 Color[3];
                EquirectSample(Source, S, T, Color);

                Color[0] = fminf(Color[0], CUBEMAP_MAX_RADIANCE);
                Color[1] = fminf(Color[1], CUBEMAP_MAX_RADIANCE);
                Color[2] = fminf(Color[2], CUBEMAP_MAX_RADIANCE);

                uint16 *Out = FacePixels + ((memory_size)Y * FaceSize + X) * 4;
                Out[0] = FloatToHalf(Color[0]);
                Out[1] = FloatToHalf(Color[1]);
                Out[2] = FloatToHalf(Color[2]);
                Out[3] = FloatToHalf(1.0f);
            }
        }
    }

    Result.Pixels   = Pixels;
    Result.FaceSize = FaceSize;
    Result.ByteSize = (uint64)FaceSize * FaceSize * FacesCount * ChannelsPerPixel * sizeof(uint16);

    return Result;
}

#endif
