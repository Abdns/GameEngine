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

void Win32SetupPlatformAPI(game_memory* GameMemory)
{
    GameMemory->PlatformReadEntireFile = Win32ReadEntireFile;
    GameMemory->PlatformFreeFileMemory = Win32FreeFileMemory;
#if ENGINE_INTERNAL
    GameMemory->DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
#endif
}
