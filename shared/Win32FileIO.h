#ifndef WIN32FILEIO_H
#define WIN32FILEIO_H

#include <windows.h>

#include "Types.h"

internal void Win32FreeFileMemory(void *Memory)
{
    if (Memory)
    {
        VirtualFree(Memory, 0, MEM_RELEASE);
    }
}

internal file_data Win32ReadEntireFile(const char *Filename)
{
    file_data Result = {};

    HANDLE FileHandle = CreateFileA(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        return Result;
    }

    LARGE_INTEGER FileSize;
    if (GetFileSizeEx(FileHandle, &FileSize))
    {
        uint32 FileSize32 = SafeTruncateUInt64((uint64)FileSize.QuadPart);
        void *Data = VirtualAlloc(0, FileSize32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (Data)
        {
            DWORD BytesRead = 0;
            if (ReadFile(FileHandle, Data, FileSize32, &BytesRead, 0) && BytesRead == FileSize32)
            {
                Result.Data = Data;
                Result.Size = FileSize32;
            }
            else
            {
                Win32FreeFileMemory(Data);
            }
        }
    }

    CloseHandle(FileHandle);

    return Result;
}

#endif
