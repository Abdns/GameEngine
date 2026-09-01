#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef float  real32;
typedef double real64;

#define internal static
#define local_persist static
#define global_variable static
#define bool32 int

#define Pi32 3.14159265359f

#define Epsilon32 1.0e-8f

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

#define HashSlotIndex(Table, HashValue) ((uint32)((HashValue) & (ArrayCount(Table) - 1)))

#define AlignPow2(Value, Alignment) (((Value) + ((Alignment) - 1)) & ~((Alignment) - 1))

#define Kilobytes(Value) ((Value) * 1024LL)
#define Megabytes(Value) (Kilobytes(Value) * 1024LL)
#define Gigabytes(Value) (Megabytes(Value) * 1024LL)
#define Terabytes(Value) (Gigabytes(Value) * 1024LL)

#if ENGINE_INTERNAL
#define Assert(Expression) if (!(Expression)) { *(int*)0 = 0; }
#else
#define Assert(Expression)
#endif

#if ENGINE_INTERNAL
#include <stdio.h>
#define DebugLog(...) printf(__VA_ARGS__)
#else
#define DebugLog(...)
#endif

inline uint32 SafeTruncateUInt64(uint64 Value)
{
	Assert(Value <= 0xFFFFFFFF);
	uint32 Result = (uint32)Value;
	return Result;
}

struct file_data
{
    void  *Data;
    uint32 Size;
};

#endif
