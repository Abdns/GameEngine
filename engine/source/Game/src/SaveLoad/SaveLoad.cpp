#define SAVE_FILES_PATH "..\\Saves"  

internal bool32 SaveGame(game_memory* Memory, const char* Filename, void* Buffer, uint32 BufferSize)
{
    uint32 Used = 0;

    return Memory->PlatformWriteEntireFile(Filename, Used, Buffer);
}

internal bool32 LoadGame(game_memory* Memory, const char* Filename)
{
    file_data File = Memory->PlatformReadEntireFile(Filename);
    if (!File.Data)
    {
        return false; 
    }

    Memory->PlatformFreeFileMemory(File.Data);

    return true;
}