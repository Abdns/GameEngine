struct win32_sound_output
{
    int32  SamplesPerSecond;
    uint32 RunningSampleIndex;
    int32  BytesPerSample;
    int32  SecondaryBufferSize;
    DWORD  ExpectedSoundBytesPerFrame;
    DWORD  SafetyBytes;
};

struct win32_sound_lock_region
{
    DWORD  ByteToLock;
    DWORD  BytesToWrite;
    bool32 IsValid;

    DWORD  PlayCursor;
    DWORD  WriteCursor;
    DWORD  ExpectedFrameBoundaryByte;
};

struct win32_debug_time_marker
{
    DWORD OutputPlayCursor;
    DWORD OutputWriteCursor;
    DWORD OutputLocation;
    DWORD OutputByteCount;
    DWORD ExpectedFlipPlayCursor;
    DWORD FlipPlayCursor;
    DWORD FlipWriteCursor;
};

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND* ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;

win32_sound_output Win32MakeSoundOutput(int32 SamplesPerSecond, int32 GameUpdateHz)
{
    win32_sound_output SoundOutput = {};
    SoundOutput.SamplesPerSecond        = SamplesPerSecond;
    SoundOutput.BytesPerSample          = sizeof(int16) * 2;
    SoundOutput.SecondaryBufferSize     = SamplesPerSecond * SoundOutput.BytesPerSample;
    SoundOutput.ExpectedSoundBytesPerFrame =
        (SamplesPerSecond * SoundOutput.BytesPerSample) / GameUpdateHz;
    SoundOutput.SafetyBytes              = (SoundOutput.ExpectedSoundBytesPerFrame * 3) / 2;
    SoundOutput.SafetyBytes             -= SoundOutput.SafetyBytes % SoundOutput.BytesPerSample;
    return SoundOutput;
}

void Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize)
{
    HMODULE DSoundLibrary = LoadLibrary(L"dsound.dll");
    if (!DSoundLibrary) return;

    direct_sound_create* DirectSoundCreate = (direct_sound_create*)GetProcAddress(DSoundLibrary, "DirectSoundCreate");

    LPDIRECTSOUND DirectSound;
    if (!DirectSoundCreate || !SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0))) return;

    WAVEFORMATEX WaveFormat = {};
    WaveFormat.wFormatTag      = WAVE_FORMAT_PCM;
    WaveFormat.nChannels       = 2;
    WaveFormat.nSamplesPerSec  = SamplesPerSecond;
    WaveFormat.wBitsPerSample  = 16;
    WaveFormat.nBlockAlign     = (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
    WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;
    WaveFormat.cbSize          = 0;

    if (SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
    {
        DSBUFFERDESC PrimaryDesc = {};
        PrimaryDesc.dwSize  = sizeof(PrimaryDesc);
        PrimaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

        LPDIRECTSOUNDBUFFER PrimaryBuffer;
        if (SUCCEEDED(DirectSound->CreateSoundBuffer(&PrimaryDesc, &PrimaryBuffer, 0)))
        {
            if (SUCCEEDED(PrimaryBuffer->SetFormat(&WaveFormat)))
            {
                OutputDebugStringA("Primary Buffer format was set\n");
            }
        }
    }

    DSBUFFERDESC SecondaryDesc = {};
    SecondaryDesc.dwSize        = sizeof(SecondaryDesc);
    SecondaryDesc.dwFlags       = 0;
    SecondaryDesc.dwBufferBytes = BufferSize;
    SecondaryDesc.lpwfxFormat   = &WaveFormat;

    if (SUCCEEDED(DirectSound->CreateSoundBuffer(&SecondaryDesc, &GlobalSecondaryBuffer, 0)))
    {
        OutputDebugStringA("Secondary Buffer was created\n");
    }
}

void Win32ClearSoundBuffer(win32_sound_output* SoundOutput)
{
    if (!GlobalSecondaryBuffer) return;

    VOID* Region1; DWORD Region1Size;
    VOID* Region2; DWORD Region2Size;

    if (SUCCEEDED(GlobalSecondaryBuffer->Lock(0, SoundOutput->SecondaryBufferSize, &Region1, &Region1Size, &Region2, &Region2Size, 0)))
    {
        uint8* DestSample = (uint8*)Region1;
        for (DWORD ByteIndex = 0; ByteIndex < Region1Size; ++ByteIndex)
        {
            *DestSample++ = 0;
        }

        DestSample = (uint8*)Region2;
        for (DWORD ByteIndex = 0; ByteIndex < Region2Size; ++ByteIndex)
        {
            *DestSample++ = 0;
        }

        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

win32_sound_lock_region Win32GetSoundLockRegion(win32_sound_output* SoundOutput, real32 SecondsLeftUntilFlip, real32 TargetSecondsPerFrame, bool32* SoundIsValid)
{
    win32_sound_lock_region Result = {};
    if (!GlobalSecondaryBuffer) return Result;

    DWORD PlayCursor;
    DWORD WriteCursor;
    if (!SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor)))
    {
        *SoundIsValid = false;
        return Result;
    }

    if (!*SoundIsValid)
    {
        SoundOutput->RunningSampleIndex = WriteCursor / SoundOutput->BytesPerSample;
        *SoundIsValid = true;
    }

    Result.ByteToLock = (SoundOutput->RunningSampleIndex * SoundOutput->BytesPerSample) % SoundOutput->SecondaryBufferSize;

    DWORD ExpectedSoundBytesPerFrame = SoundOutput->ExpectedSoundBytesPerFrame;
    DWORD ExpectedBytesUntilFlip = (DWORD)((SecondsLeftUntilFlip / TargetSecondsPerFrame) * (real32)ExpectedSoundBytesPerFrame);
    DWORD ExpectedFrameBoundaryByte = PlayCursor + ExpectedBytesUntilFlip;

    DWORD SafeWriteCursor = WriteCursor;
    if (SafeWriteCursor < PlayCursor) SafeWriteCursor += SoundOutput->SecondaryBufferSize;
    Assert(SafeWriteCursor >= PlayCursor);
    SafeWriteCursor += SoundOutput->SafetyBytes;

    bool32 AudioCardIsLowLatency = (SafeWriteCursor < ExpectedFrameBoundaryByte);

    DWORD TargetCursor;
    if (AudioCardIsLowLatency)
    {
        TargetCursor = ExpectedFrameBoundaryByte + ExpectedSoundBytesPerFrame;
    }
    else
    {
        TargetCursor = WriteCursor + ExpectedSoundBytesPerFrame + SoundOutput->SafetyBytes;
    }
    TargetCursor = TargetCursor % SoundOutput->SecondaryBufferSize;

    if (Result.ByteToLock > TargetCursor)
    {
        Result.BytesToWrite  = SoundOutput->SecondaryBufferSize - Result.ByteToLock;
        Result.BytesToWrite += TargetCursor;
    }
    else
    {
        Result.BytesToWrite = TargetCursor - Result.ByteToLock;
    }
    Result.BytesToWrite -= Result.BytesToWrite % SoundOutput->BytesPerSample;

    Result.PlayCursor                = PlayCursor;
    Result.WriteCursor               = WriteCursor;
    Result.ExpectedFrameBoundaryByte = ExpectedFrameBoundaryByte;
    Result.IsValid                   = true;

    return Result;
}

void Win32FillSoundBuffer(win32_sound_output* SoundOutput, DWORD ByteToLock, DWORD BytesToWrite,
                          game_sound_output_buffer* SourceBuffer)
{
    if (!GlobalSecondaryBuffer) return;

    VOID* Region1; DWORD Region1Size;
    VOID* Region2; DWORD Region2Size;

    if (SUCCEEDED(GlobalSecondaryBuffer->Lock(ByteToLock, BytesToWrite,
                                              &Region1, &Region1Size,
                                              &Region2, &Region2Size, 0)))
    {
        DWORD Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
        int16* SourceSample = SourceBuffer->Samples;
        int16* DestSample   = (int16*)Region1;
        for (DWORD SampleIndex = 0; SampleIndex < Region1SampleCount; ++SampleIndex)
        {
            *DestSample++ = *SourceSample++;
            *DestSample++ = *SourceSample++;
            ++SoundOutput->RunningSampleIndex;
        }

        DWORD Region2SampleCount = Region2Size / SoundOutput->BytesPerSample;
        DestSample = (int16*)Region2;
        for (DWORD SampleIndex = 0; SampleIndex < Region2SampleCount; ++SampleIndex)
        {
            *DestSample++ = *SourceSample++;
            *DestSample++ = *SourceSample++;
            ++SoundOutput->RunningSampleIndex;
        }

        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

void Win32DebugDrawVertical(win32_offscreen_buffer* Buffer, int X, int Top, int Bottom, uint32 Color)
{
    if (Top    < 0)             Top = 0;
    if (Bottom > Buffer->Height) Bottom = Buffer->Height;
    if (X < 0 || X >= Buffer->Width) return;

    uint8* Pixel = (uint8*)Buffer->Memory + X * Buffer->BytesPerPixel + Top * Buffer->Pitch;
    for (int Y = Top; Y < Bottom; ++Y)
    {
        *(uint32*)Pixel = Color;
        Pixel += Buffer->Pitch;
    }
}

void Win32DebugDrawSoundBufferMarker(win32_offscreen_buffer* Buffer,
                                     win32_sound_output* SoundOutput,
                                     real32 C, int PadX, int Top, int Bottom,
                                     DWORD Value, uint32 Color)
{
    real32 XReal32 = C * (real32)Value;
    int X = PadX + (int)XReal32;
    Win32DebugDrawVertical(Buffer, X, Top, Bottom, Color);
}

void Win32RecordDebugMarker(win32_debug_time_marker* Markers,
                            int* MarkerIndex,
                            int MarkerCount,
                            win32_sound_lock_region* LockRegion)
{
    if (!GlobalSecondaryBuffer) return;

    DWORD PlayCursor, WriteCursor;
    if (!SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor)))
        return;

    Assert(*MarkerIndex < MarkerCount);
    win32_debug_time_marker* Marker = &Markers[*MarkerIndex];
    Marker->FlipPlayCursor          = PlayCursor;
    Marker->FlipWriteCursor         = WriteCursor;
    Marker->OutputPlayCursor        = LockRegion->PlayCursor;
    Marker->OutputWriteCursor       = LockRegion->WriteCursor;
    Marker->OutputLocation          = LockRegion->ByteToLock;
    Marker->OutputByteCount         = LockRegion->BytesToWrite;
    Marker->ExpectedFlipPlayCursor  = LockRegion->ExpectedFrameBoundaryByte;

    ++*MarkerIndex;
    if (*MarkerIndex >= MarkerCount) *MarkerIndex = 0;
}

void Win32DebugSyncDisplay(win32_offscreen_buffer* Buffer,
                           int MarkerCount, win32_debug_time_marker* Markers,
                           int CurrentMarkerIndex,
                           win32_sound_output* SoundOutput, real32 TargetSecondsPerFrame)
{
    int PadX = 16;
    int PadY = 16;

    int LineHeight = 64;

    real32 C = (real32)(Buffer->Width - 2 * PadX) / (real32)SoundOutput->SecondaryBufferSize;

    for (int MarkerIndex = 0; MarkerIndex < MarkerCount; ++MarkerIndex)
    {
        win32_debug_time_marker* ThisMarker = &Markers[MarkerIndex];
        Assert(ThisMarker->OutputPlayCursor      < (DWORD)SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->OutputWriteCursor     < (DWORD)SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->OutputLocation        < (DWORD)SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->OutputByteCount       < (DWORD)SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->FlipPlayCursor        < (DWORD)SoundOutput->SecondaryBufferSize);
        Assert(ThisMarker->FlipWriteCursor       < (DWORD)SoundOutput->SecondaryBufferSize);

        DWORD PlayColor          = 0xFFFFFFFF;
        DWORD WriteColor         = 0xFFFF0000;
        DWORD ExpectedFlipColor  = 0xFFFFFF00;
        DWORD PlayWindowColor    = 0xFFFF00FF;

        int Top    = PadY;
        int Bottom = PadY + LineHeight;

        Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, Top, Bottom,
                                        ThisMarker->FlipPlayCursor, PlayColor);
        Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, Top, Bottom,
                                        (ThisMarker->FlipPlayCursor + 480 * SoundOutput->BytesPerSample) % SoundOutput->SecondaryBufferSize,
                                        PlayWindowColor);
        Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, Top, Bottom,
                                        ThisMarker->FlipWriteCursor, WriteColor);

        if (MarkerIndex == CurrentMarkerIndex)
        {
            Top    += LineHeight + PadY;
            Bottom += LineHeight + PadY;

            int FirstTop = Top;

            Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, Top, Bottom,
                                            ThisMarker->OutputPlayCursor, PlayColor);
            Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, Top, Bottom,
                                            ThisMarker->OutputWriteCursor, WriteColor);

            Top    += LineHeight + PadY;
            Bottom += LineHeight + PadY;

            Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, Top, Bottom,
                                            ThisMarker->OutputLocation, PlayColor);
            Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, Top, Bottom,
                                            (ThisMarker->OutputLocation + ThisMarker->OutputByteCount) % SoundOutput->SecondaryBufferSize,
                                            WriteColor);

            Top    += LineHeight + PadY;
            Bottom += LineHeight + PadY;

            Win32DebugDrawSoundBufferMarker(Buffer, SoundOutput, C, PadX, FirstTop, Bottom,
                                            ThisMarker->ExpectedFlipPlayCursor % SoundOutput->SecondaryBufferSize,
                                            ExpectedFlipColor);
        }
    }
}

int16* Win32StartSound(HWND Window, win32_sound_output* SoundOutput)
{
    Win32InitDSound(Window, SoundOutput->SamplesPerSecond, SoundOutput->SecondaryBufferSize);
    Win32ClearSoundBuffer(SoundOutput);
    if (GlobalSecondaryBuffer)
    {
        GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
    }
    return (int16*)VirtualAlloc(0, SoundOutput->SecondaryBufferSize,
                                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

win32_sound_lock_region Win32UpdateAudio(win32_sound_output* SoundOutput,
                                        LARGE_INTEGER FlipWallClock,
                                        real32 TargetSecondsPerFrame,
                                        bool32* SoundIsValid,
                                        game_get_sound_samples* GetSoundSamples,
                                        game_memory* GameMemory,
                                        int16* Samples)
{
    real32 FromBeginToAudioSeconds = Win32GetSecondsElapsed(FlipWallClock, Win32GetWallClock());
    real32 SecondsLeftUntilFlip    = TargetSecondsPerFrame - FromBeginToAudioSeconds;
    if (SecondsLeftUntilFlip < 0) SecondsLeftUntilFlip = 0;

    win32_sound_lock_region LockRegion =
        Win32GetSoundLockRegion(SoundOutput, SecondsLeftUntilFlip,
                                TargetSecondsPerFrame, SoundIsValid);

    if (LockRegion.IsValid)
    {
        game_sound_output_buffer SoundBuffer = {};
        SoundBuffer.SamplesPerSecond = SoundOutput->SamplesPerSecond;
        SoundBuffer.SampleCount      = LockRegion.BytesToWrite / SoundOutput->BytesPerSample;
        SoundBuffer.Samples          = Samples;

        GetSoundSamples(GameMemory, &SoundBuffer);
        Win32FillSoundBuffer(SoundOutput, LockRegion.ByteToLock,
                             LockRegion.BytesToWrite, &SoundBuffer);
    }

    return LockRegion;
}
