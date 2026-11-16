#ifndef TGA_H
#define TGA_H

#include "Types.h"
#include "Memory.h"

struct loaded_bitmap
{
    uint32 Width;
    uint32 Height;
    void  *Pixels;
};

internal loaded_bitmap ParseTGA(memory_arena *Arena, void *FileData, uint32 FileSize)
{
    loaded_bitmap Result = {};

    Assert(FileData && FileSize >= 18);

    uint8 *Bytes = (uint8 *)FileData;

    uint8  IdLength          = Bytes[0];
    uint8  ColorMapType      = Bytes[1];
    uint8  ImageType         = Bytes[2];
    uint32 ColorMapLength    = (uint32)(Bytes[5] | (Bytes[6] << 8));
    uint8  ColorMapEntrySize = Bytes[7];
    uint32 Width             = (uint32)(Bytes[12] | (Bytes[13] << 8));
    uint32 Height            = (uint32)(Bytes[14] | (Bytes[15] << 8));
    uint8  PixelDepth        = Bytes[16];
    uint8  Descriptor        = Bytes[17];

    bool32 IsUncompressed = (ImageType == 2);
    bool32 IsRLE          = (ImageType == 10);

    Assert((IsUncompressed || IsRLE) && (PixelDepth == 24 || PixelDepth == 32));
    Assert(Width && Height);

    uint32 ColorMapBytes = 0;
    if (ColorMapType == 1)
    {
        ColorMapBytes = ColorMapLength * (((uint32)ColorMapEntrySize + 7u) / 8u);
    }

    uint32 Offset = 18u + IdLength + ColorMapBytes;
    Assert(Offset <= FileSize);

    uint32 PixelCount    = Width * Height;
    uint32 BytesPerPixel = (uint32)PixelDepth / 8u;

    uint32 *Out      = PushArray(Arena, PixelCount, uint32);
    uint8  *OutBytes = (uint8 *)Out;

    uint8 *Src = Bytes + Offset;
    uint8 *End = Bytes + FileSize;

    if (IsUncompressed)
    {
        Assert(Src + (memory_size)PixelCount * BytesPerPixel <= End);

        for (uint32 i = 0; i < PixelCount; ++i)
        {
            uint8 *d = &OutBytes[i * 4];
            d[0] = Src[2];
            d[1] = Src[1];
            d[2] = Src[0];
            d[3] = (uint8)((BytesPerPixel == 4) ? Src[3] : 255);
            Src += BytesPerPixel;
        }
    }
    else
    {
        uint32 i = 0;
        while (i < PixelCount)
        {
            Assert(Src < End);

            uint8  Packet    = *Src++;
            uint32 Count     = (uint32)(Packet & 0x7F) + 1;
            bool32 RunLength = (Packet & 0x80) != 0;

            if (i + Count > PixelCount)
            {
                Count = PixelCount - i;
            }

            if (RunLength)
            {
                Assert(Src + BytesPerPixel <= End);

                uint8 B = Src[0];
                uint8 G = Src[1];
                uint8 R = Src[2];
                uint8 A = (uint8)((BytesPerPixel == 4) ? Src[3] : 255);
                Src += BytesPerPixel;

                for (uint32 c = 0; c < Count; ++c, ++i)
                {
                    uint8 *d = &OutBytes[i * 4];
                    d[0] = R;
                    d[1] = G;
                    d[2] = B;
                    d[3] = A;
                }
            }
            else
            {
                Assert(Src + (memory_size)Count * BytesPerPixel <= End);

                for (uint32 c = 0; c < Count; ++c, ++i)
                {
                    uint8 *d = &OutBytes[i * 4];
                    d[0] = Src[2];
                    d[1] = Src[1];
                    d[2] = Src[0];
                    d[3] = (uint8)((BytesPerPixel == 4) ? Src[3] : 255);
                    Src += BytesPerPixel;
                }
            }
        }
    }

    bool32 TopToBottom = (Descriptor & 0x20) != 0;
    bool32 RightToLeft = (Descriptor & 0x10) != 0;

    if (!TopToBottom)
    {
        for (uint32 y = 0; y < Height / 2; ++y)
        {
            uint32 *RowA = &Out[y * Width];
            uint32 *RowB = &Out[(Height - 1 - y) * Width];
            for (uint32 x = 0; x < Width; ++x)
            {
                uint32 t = RowA[x];
                RowA[x] = RowB[x];
                RowB[x] = t;
            }
        }
    }

    if (RightToLeft)
    {
        for (uint32 y = 0; y < Height; ++y)
        {
            uint32 *Row = &Out[y * Width];
            for (uint32 x = 0; x < Width / 2; ++x)
            {
                uint32 t = Row[x];
                Row[x] = Row[Width - 1 - x];
                Row[Width - 1 - x] = t;
            }
        }
    }

    Result.Width  = Width;
    Result.Height = Height;
    Result.Pixels = Out;
    return Result;
}

#endif
