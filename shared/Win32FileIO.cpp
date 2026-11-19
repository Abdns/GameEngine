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

#if ENGINE_INTERNAL

internal bool32 DEBUGPlatformWriteEntireFile(char *Filename, uint32 MemorySize, void *Memory)
{
    HANDLE FileHandle = CreateFileA(Filename, GENERIC_WRITE, 0, 0,
                                    CREATE_ALWAYS, 0, 0);
    Assert(FileHandle != INVALID_HANDLE_VALUE);

    DWORD BytesWritten = 0;
    bool32 Wrote = WriteFile(FileHandle, Memory, MemorySize, &BytesWritten, 0);
    Assert(Wrote && BytesWritten == MemorySize);

    CloseHandle(FileHandle);

    return true;
}

#endif

internal bool32 Win32WriteEntireFile(const char *Filename, uint32 MemorySize, void *Memory)
{
    char TempName[MAX_PATH];
    lstrcpynA(TempName, Filename, MAX_PATH - 4);
    lstrcatA(TempName, ".tmp");

    HANDLE FileHandle = CreateFileA(TempName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD BytesWritten = 0;
    bool32 Wrote = WriteFile(FileHandle, Memory, MemorySize, &BytesWritten, 0) && BytesWritten == MemorySize;
    Wrote = Wrote && FlushFileBuffers(FileHandle);
    CloseHandle(FileHandle);

    if (!Wrote)
    {
        DeleteFileA(TempName);
        return false;
    }

    return MoveFileExA(TempName, Filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
