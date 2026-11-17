#if ENGINE_INTERNAL

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile)
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

// Writes to a temporary file first, so a crash during the write keeps the previous file intact
PLATFORM_WRITE_ENTIRE_FILE(Win32WriteEntireFile)
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

void Win32SetupPlatformAPI(game_memory* GameMemory)
{
    GameMemory->PlatformReadEntireFile = Win32ReadEntireFile;
    GameMemory->PlatformWriteEntireFile = Win32WriteEntireFile;
    GameMemory->PlatformFreeFileMemory = Win32FreeFileMemory;
#if ENGINE_INTERNAL
    GameMemory->DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
#endif
}
