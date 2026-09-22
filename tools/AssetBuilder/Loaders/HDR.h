#ifndef HDR_H
#define HDR_H

#include "Types.h"
#include "Memory.h"
#include "Half.h"

struct loaded_hdr
{
    uint16 *Pixels;
    uint32  Width;
    uint32  Height;
    uint64  ByteSize;
};

internal void RGBEToHalf(uint8 *RGBE, uint16 *Out)
{
    uint8 E = RGBE[3];
    if (!E)
    {
        Out[0] = 0;
        Out[1] = 0;
        Out[2] = 0;
        Out[3] = FloatToHalf(1.0f);
        return;
    }

    real32 Scale = 1.0f;
    int32  Shift = (int32)E - (128 + 8);
    if (Shift > 0)
    {
        for (int32 i = 0; i < Shift; ++i)  Scale *= 2.0f;
    }
    else
    {
        for (int32 i = 0; i < -Shift; ++i) Scale *= 0.5f;
    }

    Out[0] = FloatToHalf((real32)RGBE[0] * Scale);
    Out[1] = FloatToHalf((real32)RGBE[1] * Scale);
    Out[2] = FloatToHalf((real32)RGBE[2] * Scale);
    Out[3] = FloatToHalf(1.0f);
}

internal bool32 HDRReadLine(uint8 *Data, uint32 Size, uint32 *At, char *Line, uint32 LineMax)
{
    uint32 Length = 0;
    while (*At < Size)
    {
        uint8 C = Data[(*At)++];
        if (C == '\n')
        {
            Line[Length] = 0;
            return true;
        }

        if (Length + 1 < LineMax)
        {
            Line[Length++] = (char)C;
        }
    }

    return false;
}

internal loaded_hdr ParseHDR(memory_arena *Arena, void *FileData, uint32 FileSize)
{
    loaded_hdr Result = {};

    uint8 *Data = (uint8 *)FileData;
    uint32 At   = 0;

    char Line[256];
    if (!HDRReadLine(Data, FileSize, &At, Line, sizeof(Line)) || Line[0] != '#' || Line[1] != '?')
    {
        DebugLog("HDR: missing #? signature\n");
        return Result;
    }

    while (HDRReadLine(Data, FileSize, &At, Line, sizeof(Line)))
    {
        if (!Line[0])
        {
            break;
        }
    }

    if (!HDRReadLine(Data, FileSize, &At, Line, sizeof(Line)))
    {
        DebugLog("HDR: missing resolution line\n");
        return Result;
    }

    uint32 Width  = 0;
    uint32 Height = 0;
    {
        uint32 i = 0;
        while (Line[i] && !(Line[i] >= '0' && Line[i] <= '9')) ++i;
        while (Line[i] >= '0' && Line[i] <= '9') Height = Height * 10 + (uint32)(Line[i++] - '0');
        while (Line[i] && !(Line[i] >= '0' && Line[i] <= '9')) ++i;
        while (Line[i] >= '0' && Line[i] <= '9') Width = Width * 10 + (uint32)(Line[i++] - '0');
    }

    Assert(Width && Height);

    uint16 *Pixels = PushArray(Arena, (memory_size)Width * Height * 4, uint16);
    uint8  *Scan   = PushArray(Arena, (memory_size)Width * 4, uint8);

    for (uint32 Y = 0; Y < Height; ++Y)
    {
        Assert(At + 4 <= FileSize);

        bool32 NewRLE = (Data[At] == 2 && Data[At + 1] == 2 &&
                         (((uint32)Data[At + 2] << 8) | Data[At + 3]) == Width && Width >= 8 && Width < 0x8000);

        if (NewRLE)
        {
            At += 4;

            for (uint32 Component = 0; Component < 4; ++Component)
            {
                uint32 X = 0;
                while (X < Width)
                {
                    Assert(At < FileSize);

                    uint8 Count = Data[At++];
                    if (Count > 128)
                    {
                        uint8 Value = Data[At++];
                        for (uint32 i = 0; i < (uint32)(Count - 128) && X < Width; ++i)
                        {
                            Scan[X++ * 4 + Component] = Value;
                        }
                    }
                    else
                    {
                        for (uint32 i = 0; i < Count && X < Width; ++i)
                        {
                            Scan[X++ * 4 + Component] = Data[At++];
                        }
                    }
                }
            }
        }
        else
        {
            for (uint32 X = 0; X < Width; ++X)
            {
                Assert(At + 4 <= FileSize);

                Scan[X * 4 + 0] = Data[At + 0];
                Scan[X * 4 + 1] = Data[At + 1];
                Scan[X * 4 + 2] = Data[At + 2];
                Scan[X * 4 + 3] = Data[At + 3];
                At += 4;
            }
        }

        uint16 *Row = &Pixels[(memory_size)Y * Width * 4];
        for (uint32 X = 0; X < Width; ++X)
        {
            RGBEToHalf(&Scan[X * 4], &Row[X * 4]);
        }
    }

    Result.Pixels   = Pixels;
    Result.Width    = Width;
    Result.Height   = Height;
    Result.ByteSize = (uint64)Width * Height * 4 * sizeof(uint16);

    return Result;
}

#endif
