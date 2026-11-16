struct win32_game_code
{
    HMODULE  GameCodeDLL;
    FILETIME DLLLastWriteTime;
    game_update_and_render* UpdateAndRender;
    game_get_sound_samples* GetSoundSamples;
    bool32 IsValid;
};

struct win32_exe_paths
{
    char EXEFileName[MAX_PATH];
    char* OnePastLastSlash;
    char SourceGameCodeDLLFullPath[MAX_PATH];
    char TempGameCodeDLLFullPath[MAX_PATH];
    char GameCodeLockFullPath[MAX_PATH];
};

internal GAME_UPDATE_AND_RENDER(GameUpdateAndRenderStub)
{
}

internal GAME_GET_SOUND_SAMPLES(GameGetSoundSamplesStub)
{
}

internal void Win32BuildEXEPathFileName(char* EXEPath, char* OnePastLastSlash, char* FileName, int DestCount, char* Dest)
{
    int PrefixLen = (int)(OnePastLastSlash - EXEPath);
    int i = 0;
    for (; i < PrefixLen && i < DestCount - 1; ++i) Dest[i] = EXEPath[i];
    for (int j = 0; FileName[j] && i < DestCount - 1; ++j, ++i) Dest[i] = FileName[j];
    Dest[i] = 0;
}

void Win32GetEXEPaths(win32_exe_paths* Paths)
{
    GetModuleFileNameA(0, Paths->EXEFileName, sizeof(Paths->EXEFileName));

    Paths->OnePastLastSlash = Paths->EXEFileName;
    for (char* Scan = Paths->EXEFileName; *Scan; ++Scan)
    {
        if (*Scan == '\\') Paths->OnePastLastSlash = Scan + 1;
    }

    Win32BuildEXEPathFileName(Paths->EXEFileName, Paths->OnePastLastSlash, (char*)"Game.dll", sizeof(Paths->SourceGameCodeDLLFullPath), Paths->SourceGameCodeDLLFullPath);
    Win32BuildEXEPathFileName(Paths->EXEFileName, Paths->OnePastLastSlash, (char*)"Game_temp.dll", sizeof(Paths->TempGameCodeDLLFullPath), Paths->TempGameCodeDLLFullPath);
    Win32BuildEXEPathFileName(Paths->EXEFileName, Paths->OnePastLastSlash, (char*)"lock.tmp", sizeof(Paths->GameCodeLockFullPath), Paths->GameCodeLockFullPath);
}

FILETIME Win32GetLastWriteTime(char* Filename)
{
    FILETIME LastWriteTime = {};
    WIN32_FILE_ATTRIBUTE_DATA Data;
    if (GetFileAttributesExA(Filename, GetFileExInfoStandard, &Data))
    {
        LastWriteTime = Data.ftLastWriteTime;
    }
    return LastWriteTime;
}

win32_game_code Win32LoadGameCode(char* SourceDLLName, char* TempDLLName)
{
    win32_game_code Result = {};

    Result.DLLLastWriteTime = Win32GetLastWriteTime(SourceDLLName);

    CopyFileA(SourceDLLName, TempDLLName, FALSE);

    Result.GameCodeDLL = LoadLibraryA(TempDLLName);
    if (Result.GameCodeDLL)
    {
        Result.UpdateAndRender = (game_update_and_render*)GetProcAddress(Result.GameCodeDLL, "GameUpdateAndRender");
        Result.GetSoundSamples = (game_get_sound_samples*)GetProcAddress(Result.GameCodeDLL, "GameGetSoundSamples");
        Result.IsValid = (Result.UpdateAndRender != 0) && (Result.GetSoundSamples != 0);
    }

    if (!Result.IsValid)
    {
        Result.UpdateAndRender = GameUpdateAndRenderStub;
        Result.GetSoundSamples = GameGetSoundSamplesStub;
    }

    return Result;
}

void Win32UnloadGameCode(win32_game_code* GameCode)
{
    if (GameCode->GameCodeDLL)
    {
        FreeLibrary(GameCode->GameCodeDLL);
        GameCode->GameCodeDLL = 0;
    }
    GameCode->IsValid = false;
    GameCode->UpdateAndRender = GameUpdateAndRenderStub;
    GameCode->GetSoundSamples = GameGetSoundSamplesStub;
}

void Win32ReloadGameCodeIfChanged(win32_game_code* Game,
                                  char* SourceDLLPath,
                                  char* TempDLLPath,
                                  char* LockPath)
{
    FILETIME NewDLLWriteTime = Win32GetLastWriteTime(SourceDLLPath);
    if (CompareFileTime(&NewDLLWriteTime, &Game->DLLLastWriteTime) != 0)
    {
        WIN32_FILE_ATTRIBUTE_DATA Ignored;
        if (!GetFileAttributesExA(LockPath, GetFileExInfoStandard, &Ignored))
        {
            Win32UnloadGameCode(Game);
            *Game = Win32LoadGameCode(SourceDLLPath, TempDLLPath);
        }
    }
}
