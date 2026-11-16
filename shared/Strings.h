#ifndef STRINGS_H
#define STRINGS_H

#include "Types.h"

internal bool32 StringsAreEqual(const char *a, const char *b)
{
    while (*a && (*a == *b))
    {
        ++a;
        ++b;
    }
    return *a == *b;
}

internal uint32 AppendString(char *dest, uint32 destSize, uint32 at, const char *src)
{
    while (*src && (at + 1) < destSize)
    {
        dest[at++] = *src++;
    }
    if (at < destSize)
    {
        dest[at] = 0;
    }
    return at;
}

#endif
