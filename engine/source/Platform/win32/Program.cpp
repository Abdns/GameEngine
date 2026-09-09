#include <windows.h>
#include <Xinput.h>
#include <dsound.h>
#include <Wingdi.h>
#include "Types.h"
#include "Memory.h"
#include "Win32FileIO.h"
#include "PlatformAPI.h"
#include "Vulkan.h"

extern bool32 isRunning;
extern int64 GlobalPerfCountFrequency;

struct win32_state
{
    uint64 TotalSize;
    void*  GameMemoryBlock;

    HANDLE RecordingHandle;
    int    InputRecordingIndex;

    HANDLE PlaybackHandle;
    int    InputPlayingIndex;

    char   EXEFileName[MAX_PATH];
    char*  OnePastLastSlash;
};

struct win32_offscreen_buffer
{
    BITMAPINFO Info;
    void* Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
};

extern win32_offscreen_buffer GlobalBackBuffer;

inline LARGE_INTEGER Win32GetWallClock(void)
{
    LARGE_INTEGER Result;
    QueryPerformanceCounter(&Result);

    return Result;
}

inline real32 Win32GetSecondsElapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
    return (real32)(End.QuadPart - Start.QuadPart) / (real32)GlobalPerfCountFrequency;
}

#include "Win32Replay.cpp"
#include "Win32Window.cpp"
#include "Win32Input.cpp"
#include "Win32GDI.cpp"
#include "Win32Sound.cpp"
#include "Win32FileIO.cpp"
#include "Win32Timer.cpp"
#include "Win32GameCode.cpp"
#include "Win32Memory.cpp"

#include "VulkanRenderer.cpp"

int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode)
{
    bool32 SleepIsGranular;
    Win32InitTimer(&SleepIsGranular);

    win32_exe_paths Paths;
    Win32GetEXEPaths(&Paths);

    win32_state State = {};
    for (int i = 0; Paths.EXEFileName[i]; ++i)
    {
        State.EXEFileName[i] = Paths.EXEFileName[i];
    }
    State.OnePastLastSlash = State.EXEFileName + (Paths.OnePastLastSlash - Paths.EXEFileName);

    Win32LoadXInput();
    Win32ResizeDIB(&GlobalBackBuffer, 960, 540);

    HWND Window = Win32CreateMainWindow(Instance, L"MyEngine", L"Window", 960, 540);
    Assert(Window);

    HDC DeviceContext = GetDC(Window);

    int32  GameUpdateHz = Win32GetMonitorRefreshHz(DeviceContext);
    real32 TargetSecondsPerFrame = 1.0f / (real32)GameUpdateHz;

    win32_sound_output SoundOutput = Win32MakeSoundOutput(48000, GameUpdateHz);
    int16* Samples = Win32StartSound(Window, &SoundOutput);

    game_memory GameMemory = {};
    Win32AllocateGameMemory(&GameMemory, &State);
    Win32SetupPlatformAPI(&GameMemory);
    Assert(Samples && GameMemory.PermanentStorage);

    uint32 PlatformMemorySize = (uint32)Megabytes(16);
    memory_arena PlatformArena;
    InitializeArena(&PlatformArena, PlatformMemorySize, VirtualAlloc(0, PlatformMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));

    uint32 RenderMemorySize = (uint32)Megabytes(4);
    void  *RenderMemory     = PushSize(&PlatformArena, RenderMemorySize);

    game_input  Input[2] = {};
    game_input* NewInput = &Input[0];
    game_input* OldInput = &Input[1];
    Win32PrimeMouseInput(Window, NewInput, OldInput);

    const char *RendererError = InitVulkan(Instance, Window);
    if (RendererError)
    {
        MessageBoxA(Window, RendererError, "MyEngine", MB_OK | MB_ICONERROR);
        return 0;
    }

    isRunning = true;
    bool32 SoundIsValid = false;

#if ENGINE_INTERNAL
    win32_debug_time_marker DebugTimeMarkers[30] = {};
    int DebugTimeMarkerIndex = 0;
#endif

    win32_game_code Game = Win32LoadGameCode(Paths.SourceGameCodeDLLFullPath,Paths.TempGameCodeDLLFullPath);

    LARGE_INTEGER LastCounter = Win32GetWallClock();
    LARGE_INTEGER FlipWallClock = Win32GetWallClock();
    LARGE_INTEGER LastFrameTime = Win32GetWallClock();
    int64 LastCycleCount = __rdtsc();

    while (isRunning)
    {
        Win32ReloadGameCodeIfChanged(&Game, Paths.SourceGameCodeDLLFullPath, Paths.TempGameCodeDLLFullPath, Paths.GameCodeLockFullPath);

        LARGE_INTEGER FrameTime = Win32GetWallClock();
        real32 FrameDt = Win32GetSecondsElapsed(LastFrameTime, FrameTime);
        LastFrameTime = FrameTime;

        NewInput->dtForFrame = (FrameDt > 0.25f) ? 0.25f : FrameDt;
        Win32ProcessInput(NewInput, OldInput, &State, Window);
#if ENGINE_INTERNAL
        Win32UpdateRecordAndPlayback(&State, NewInput);
#endif

        render_commands RenderCommands = InitRenderCommands(RenderMemory, RenderMemorySize);
        Game.UpdateAndRender(&GameMemory, NewInput, &RenderCommands);

        win32_sound_lock_region LockRegion = Win32UpdateAudio(&SoundOutput, FlipWallClock, TargetSecondsPerFrame, &SoundIsValid, Game.GetSoundSamples, &GameMemory, Samples);

        real32 MSPerFrame = Win32WaitForFrameEnd(&LastCounter, TargetSecondsPerFrame, SleepIsGranular);

#if ENGINE_INTERNAL
        Win32DebugSyncDisplay(&GlobalBackBuffer, ArrayCount(DebugTimeMarkers), DebugTimeMarkers, DebugTimeMarkerIndex - 1, &SoundOutput, TargetSecondsPerFrame);
#endif
        RenderVulkanFrame(&RenderCommands);
        FlipWallClock = Win32GetWallClock();

#if ENGINE_INTERNAL
        Win32RecordDebugMarker(DebugTimeMarkers, &DebugTimeMarkerIndex, ArrayCount(DebugTimeMarkers), &LockRegion);
#endif

        game_input* Temp = NewInput; NewInput = OldInput; OldInput = Temp;

        int64 EndCycleCount = __rdtsc();
        int64 CycleElapsed = EndCycleCount - LastCycleCount;
        LastCycleCount = EndCycleCount;
        Win32OutputFrameStats(MSPerFrame, CycleElapsed);
    }

    return 0;
}
