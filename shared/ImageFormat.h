#ifndef IMAGEFORMAT_H
#define IMAGEFORMAT_H

#include "Types.h"

enum image_format : uint32
{
    ImageFormat_RGBA8 = 0,
    ImageFormat_RGBA16F = 1,
};

inline uint32 ImageFormatBytes(image_format Format)
{
    return (Format == ImageFormat_RGBA16F) ? 8u : 4u;
}

#endif
