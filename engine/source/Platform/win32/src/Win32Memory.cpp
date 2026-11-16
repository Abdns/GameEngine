void Win32AllocateGameMemory(game_memory* GameMemory, win32_state* State)
{
#if ENGINE_INTERNAL
    LPVOID BaseAddress = (LPVOID)Terabytes(2);
#else
    LPVOID BaseAddress = 0;
#endif

    GameMemory->PermanentStorageSize = Megabytes(128);

    State->TotalSize = GameMemory->PermanentStorageSize;
    State->GameMemoryBlock = VirtualAlloc(BaseAddress, (size_t)State->TotalSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    GameMemory->PermanentStorage = State->GameMemoryBlock;
}
