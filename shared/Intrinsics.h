#ifndef INTRINSICS_H
#define INTRINSICS_H

#include "Types.h"
#include <math.h>
#include <intrin.h>
#include <stdlib.h>

inline int32 SignOf(int32 Value)
{
    return (Value >= 0) ? 1 : -1;
}

inline real32 SquareRoot(real32 Real32)
{
    return sqrtf(Real32);
}

inline real32 Abs(real32 Real32)
{
    return (real32)fabs(Real32);
}

inline uint32 RotateLeft(uint32 Value, int32 Amount)
{
    return _rotl(Value, Amount);
}

inline uint32 RotateRight(uint32 Value, int32 Amount)
{
    return _rotr(Value, Amount);
}

inline int32 RoundReal32ToInt32(real32 Real32)
{

    return (int32)(Real32 + 0.5f);
}

inline uint32 RoundReal32ToUInt32(real32 Real32)
{
    return (uint32)(Real32 + 0.5f);
}

inline int32 FloorReal32ToInt32(real32 Real32)
{
    return (int32)floorf(Real32);
}

inline int32 TruncateReal32ToInt32(real32 Real32)
{

    return (int32)Real32;
}

inline real32 Sin(real32 Angle)
{
    return sinf(Angle);
}

inline real32 Cos(real32 Angle)
{
    return cosf(Angle);
}

inline real32 ATan2(real32 Y, real32 X)
{
    return atan2f(Y, X);
}

#endif
