internal void Win32BuildPath(char* EXEPath, char* OnePastLastSlash, char* FileName, int DestCount, char* Dest)
{
    int PrefixLen = (int)(OnePastLastSlash - EXEPath);
    int i = 0;
    for (; i < PrefixLen && i < DestCount - 1; ++i) Dest[i] = EXEPath[i];
    for (int j = 0; FileName[j] && i < DestCount - 1; ++j, ++i) Dest[i] = FileName[j];
    Dest[i] = 0;
}

void Win32GetInputFileLocation(win32_state* State, bool32 InputStream, int SlotIndex, int DestCount, char* Dest)
{
    char Filename[64];
    // v2 stores platform_input and the updated game state; old snapshots are incompatible.
    wsprintfA(Filename, "replay_%d_%s_v2.hmi", SlotIndex, InputStream ? "input" : "state");
    Win32BuildPath(State->EXEFileName, State->OnePastLastSlash, Filename, DestCount, Dest);
}

void Win32BeginRecordingInput(win32_state* State, int InputRecordingIndex)
{
    char Filename[MAX_PATH];

    State->InputRecordingIndex = InputRecordingIndex;

    Win32GetInputFileLocation(State, false, InputRecordingIndex, sizeof(Filename), Filename);
    HANDLE SnapshotHandle = CreateFileA(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (SnapshotHandle != INVALID_HANDLE_VALUE)
    {
        DWORD BytesWritten;
        WriteFile(SnapshotHandle, State->GameMemoryBlock, (DWORD)State->TotalSize, &BytesWritten, 0);
        CloseHandle(SnapshotHandle);
    }

    Win32GetInputFileLocation(State, true, InputRecordingIndex, sizeof(Filename), Filename);
    State->RecordingHandle = CreateFileA(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
}

void Win32EndRecordingInput(win32_state* State)
{
    if (State->RecordingHandle && State->RecordingHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(State->RecordingHandle);
        State->RecordingHandle = 0;
    }
    State->InputRecordingIndex = 0;
}

void Win32BeginInputPlayBack(win32_state* State, int InputPlayingIndex)
{
    char Filename[MAX_PATH];

    State->InputPlayingIndex = InputPlayingIndex;

    Win32GetInputFileLocation(State, false, InputPlayingIndex, sizeof(Filename), Filename);
    HANDLE SnapshotHandle = CreateFileA(Filename, GENERIC_READ, 0, 0,
                                        OPEN_EXISTING, 0, 0);
    if (SnapshotHandle != INVALID_HANDLE_VALUE)
    {
        DWORD BytesRead;
        ReadFile(SnapshotHandle, State->GameMemoryBlock,(DWORD)State->TotalSize, &BytesRead, 0);
        CloseHandle(SnapshotHandle);
    }

    Win32GetInputFileLocation(State, true, InputPlayingIndex, sizeof(Filename), Filename);
    State->PlaybackHandle = CreateFileA(Filename, GENERIC_READ, 0, 0,
                                        OPEN_EXISTING, 0, 0);
}

void Win32EndInputPlayBack(win32_state* State)
{
    if (State->PlaybackHandle && State->PlaybackHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(State->PlaybackHandle);
        State->PlaybackHandle = 0;
    }
    State->InputPlayingIndex = 0;
}

void Win32RecordInput(win32_state* State, platform_input* NewInput)
{
    DWORD BytesWritten;
    WriteFile(State->RecordingHandle, NewInput, sizeof(*NewInput), &BytesWritten, 0);
}

void Win32PlayBackInput(win32_state* State, platform_input* NewInput)
{
    DWORD BytesRead = 0;
    if (ReadFile(State->PlaybackHandle, NewInput, sizeof(*NewInput), &BytesRead, 0))
    {
        if (BytesRead == 0)
        {

            int PlayingIndex = State->InputPlayingIndex;
            Win32EndInputPlayBack(State);
            Win32BeginInputPlayBack(State, PlayingIndex);
            ReadFile(State->PlaybackHandle, NewInput, sizeof(*NewInput), &BytesRead, 0);
        }
    }
}

void Win32UpdateRecordAndPlayback(win32_state* State, platform_input* NewInput)
{
    if (State->InputRecordingIndex) Win32RecordInput(State, NewInput);
    if (State->InputPlayingIndex)   Win32PlayBackInput(State, NewInput);
}
