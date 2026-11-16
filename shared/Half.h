#ifndef HALF_H
#define HALF_H

#include "Types.h"

internal uint16 FloatToHalf(real32 Value)
{
    union { real32 F; uint32 U; } In;
    In.F = Value;

    uint32 Bits = In.U;
    uint32 Sign = (Bits >> 16) & 0x8000;
    int32  Exp  = (int32)((Bits >> 23) & 0xFF) - 127 + 15;
    uint32 Mant = Bits & 0x7FFFFF;

    if (Exp <= 0)
    {
        return (uint16)Sign;
    }

    if (Exp >= 31)
    {
        return (uint16)(Sign | 0x7BFF);
    }

    return (uint16)(Sign | ((uint32)Exp << 10) | (Mant >> 13));
}

internal real32 HalfToFloat(uint16 Half)
{
    uint32 Sign = (uint32)(Half & 0x8000) << 16;
    uint32 Exp  = (uint32)(Half >> 10) & 0x1F;
    uint32 Mant = (uint32)(Half & 0x3FF);

    union { real32 F; uint32 U; } Out;

    if (!Exp)
    {
        Out.U = Sign;
        return Out.F;
    }

    if (Exp == 31)
    {
        Out.U = Sign | 0x7F800000 | (Mant << 13);
        return Out.F;
    }

    Out.U = Sign | ((Exp - 15 + 127) << 23) | (Mant << 13);
    return Out.F;
}

#endif
