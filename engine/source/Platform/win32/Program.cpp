#include <windows.h>
#include <Xinput.h>
#include <dsound.h>
#include <Wingdi.h>
#include <stdio.h>
#include <string.h>
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

struct win32_gi_options
{
    bool32 Enabled;
    bool32 Hidden;
    uint32 Scene;
    uint32 View;
    uint32 FrameLimit;
};

internal bool32 Win32ParseGiNumber(const char *Text, uint32 Min, uint32 Max, uint32 *Result)
{
    if (!*Text) return false;
    uint32 Value = 0;
    while (*Text)
    {
        if (*Text < '0' || *Text > '9') return false;
        uint32 Digit = (uint32)(*Text++ - '0');
        if (Digit > Max || Value > (Max - Digit) / 10) return false;
        Value = Value * 10 + Digit;
    }
    if (Value < Min) return false;
    *Result = Value;
    return true;
}

internal bool32 Win32ParseGiOptions(const char *CommandLine, win32_gi_options *Options)
{
    while (*CommandLine)
    {
        while (*CommandLine == ' ' || *CommandLine == '\t') ++CommandLine;
        if (!*CommandLine) break;
        char Token[128];
        uint32 Length = 0;
        bool32 Quoted = *CommandLine == '"';
        if (Quoted) ++CommandLine;
        while (*CommandLine && (Quoted ? *CommandLine != '"' : (*CommandLine != ' ' && *CommandLine != '\t')))
        {
            if (Length + 1 >= sizeof(Token)) return false;
            Token[Length++] = *CommandLine++;
        }
        if (Quoted)
        {
            if (*CommandLine != '"') return false;
            ++CommandLine;
        }
        Token[Length] = 0;

        if (strncmp(Token, "--gi-", 5) != 0) continue;
        Options->Enabled = true;
        if (!strcmp(Token, "--gi-hidden")) Options->Hidden = true;
        else if (!strncmp(Token, "--gi-scene=", 11))
        {
            if (!Win32ParseGiNumber(Token + 11, 1, 4, &Options->Scene)) return false;
        }
        else if (!strncmp(Token, "--gi-view=", 10))
        {
            if (!Win32ParseGiNumber(Token + 10, 0, 6, &Options->View)) return false;
        }
        else if (!strncmp(Token, "--gi-frames=", 12))
        {
            if (!Win32ParseGiNumber(Token + 12, 1, 10000, &Options->FrameLimit)) return false;
        }
        else return false;
    }
    // A hidden process must finish without requiring an invisible close button.
    if (Options->Hidden && !Options->FrameLimit) Options->FrameLimit = 60;
    return true;
}

internal HWND Win32CreateGiTestWindow(HINSTANCE Instance)
{
    const wchar_t *ClassName = L"MyEngineGiValidation";
    WNDCLASS WindowsClass = CreateWindowClass(CS_HREDRAW | CS_VREDRAW, Win32MainWindowCallback, Instance, ClassName);
    RegisterClass(&WindowsClass);
    // Omit WS_VISIBLE at creation so a test never flashes a window or takes focus.
    return CreateWindowEx(0, ClassName, L"GI validation", WS_OVERLAPPEDWINDOW, 50, 50, 960, 540, 0, 0, Instance, 0);
}

internal void Win32GiNeutralInput(HWND Window, game_input *Input, real32 DeltaTime)
{
    ZeroStruct(*Input);
    Input->dtForFrame = DeltaTime;
    RECT Client;
    GetClientRect(Window, &Client);
    Input->RenderWidth = Client.right - Client.left;
    Input->RenderHeight = Client.bottom - Client.top;
    MSG Message;
    while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
    {
        if (Message.message == WM_QUIT) isRunning = false;
        else if (Message.message != WM_KEYDOWN && Message.message != WM_KEYUP &&
                 Message.message != WM_SYSKEYDOWN && Message.message != WM_SYSKEYUP)
        {
            TranslateMessage(&Message);
            DispatchMessage(&Message);
        }
    }
}

int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode)
{
    win32_gi_options GiOptions = {};
    if (!Win32ParseGiOptions(CommandLine, &GiOptions))
    {
        DebugLog("Invalid GI options. Use --gi-scene=1..4 --gi-view=0..6 --gi-frames=1..10000 [--gi-hidden]\n");
        return 2;
    }
    if (GiOptions.Enabled)
    {
        DebugLog("GI validation startup: scene=%u view=%u frames=%u hidden=%d\n", GiOptions.Scene, GiOptions.View, GiOptions.FrameLimit, GiOptions.Hidden);
    }
    bool32 SleepIsGranular;
    Win32InitTimer(&SleepIsGranular);

    win32_exe_paths Paths;
    Win32GetEXEPaths(&Paths);
    if (GiOptions.Enabled)
    {
        char TempDLLName[64];
        snprintf(TempDLLName, sizeof(TempDLLName), "Game_gi_%lu.dll", GetCurrentProcessId());
        Win32BuildEXEPathFileName(Paths.EXEFileName, Paths.OnePastLastSlash, TempDLLName, sizeof(Paths.TempGameCodeDLLFullPath), Paths.TempGameCodeDLLFullPath);
    }

    win32_state State = {};
    for (int i = 0; Paths.EXEFileName[i]; ++i)
    {
        State.EXEFileName[i] = Paths.EXEFileName[i];
    }
    State.OnePastLastSlash = State.EXEFileName + (Paths.OnePastLastSlash - Paths.EXEFileName);

    Win32LoadXInput();
    Win32ResizeDIB(&GlobalBackBuffer, 960, 540);

    HWND Window = GiOptions.Hidden ? Win32CreateGiTestWindow(Instance) : Win32CreateMainWindow(Instance, L"MyEngine", L"Window", 960, 540);
    Assert(Window);

    HDC DeviceContext = GetDC(Window);

    int32  GameUpdateHz = Win32GetMonitorRefreshHz(DeviceContext);
    real32 TargetSecondsPerFrame = 1.0f / (real32)GameUpdateHz;

    win32_sound_output SoundOutput = Win32MakeSoundOutput(48000, GameUpdateHz);
    int16* Samples = GiOptions.Hidden ? 0 : Win32StartSound(Window, &SoundOutput);

    game_memory GameMemory = {};
    Win32AllocateGameMemory(&GameMemory, &State);
    Win32SetupPlatformAPI(&GameMemory);
    GameMemory.GiValidationRun = GiOptions.Enabled;
    GameMemory.GiStartupScene = GiOptions.Scene;
    GameMemory.GiStartupView = GiOptions.View;
    Assert((Samples || GiOptions.Hidden) && GameMemory.PermanentStorage);

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
        if (GiOptions.Enabled) DebugLog("GI validation renderer failed: %s\n", RendererError);
        if (!GiOptions.Hidden) MessageBoxA(Window, RendererError, "MyEngine", MB_OK | MB_ICONERROR);
        return GiOptions.Enabled ? 3 : 0;
    }

    isRunning = true;
    bool32 SoundIsValid = false;

#if ENGINE_INTERNAL
    win32_debug_time_marker DebugTimeMarkers[30] = {};
    int DebugTimeMarkerIndex = 0;
#endif

    win32_game_code Game = Win32LoadGameCode(Paths.SourceGameCodeDLLFullPath,Paths.TempGameCodeDLLFullPath);
    if (GiOptions.Enabled && !Game.IsValid)
    {
        DebugLog("GI validation failed to load matching Game.dll\n");
        Win32UnloadGameCode(&Game);
        DeleteFileA(Paths.TempGameCodeDLLFullPath);
        return 3;
    }

    LARGE_INTEGER LastCounter = Win32GetWallClock();
    LARGE_INTEGER FlipWallClock = Win32GetWallClock();
    LARGE_INTEGER LastFrameTime = Win32GetWallClock();
    int64 LastCycleCount = __rdtsc();
    uint64 FirstGiFrame = GlobalRenderer.Context.frameIndex;
    uint64 LastGiFrame = FirstGiFrame;
    uint32 GiStalledFrames = 0;
    int ExitCode = 0;

    while (isRunning)
    {
        if (!GiOptions.Enabled)
        {
            Win32ReloadGameCodeIfChanged(&Game, Paths.SourceGameCodeDLLFullPath, Paths.TempGameCodeDLLFullPath, Paths.GameCodeLockFullPath);
        }

        LARGE_INTEGER FrameTime = Win32GetWallClock();
        real32 FrameDt = Win32GetSecondsElapsed(LastFrameTime, FrameTime);
        LastFrameTime = FrameTime;

        if (GiOptions.FrameLimit)
        {
            // A bounded validation run has identical motion and no desktop input.
            Win32GiNeutralInput(Window, NewInput, 1.0f / 60.0f);
        }
        else
        {
            NewInput->dtForFrame = (FrameDt > 0.25f) ? 0.25f : FrameDt;
            Win32ProcessInput(NewInput, OldInput, &State, Window);
        }
#if ENGINE_INTERNAL
        if (!GiOptions.Enabled) Win32UpdateRecordAndPlayback(&State, NewInput);
#endif

        render_commands RenderCommands = InitRenderCommands(RenderMemory, RenderMemorySize);
        Game.UpdateAndRender(&GameMemory, NewInput, &RenderCommands);

        win32_sound_lock_region LockRegion = {};
        if (!GiOptions.Hidden)
        {
            LockRegion = Win32UpdateAudio(&SoundOutput, FlipWallClock, TargetSecondsPerFrame, &SoundIsValid, Game.GetSoundSamples, &GameMemory, Samples);
        }

        real32 MSPerFrame = Win32WaitForFrameEnd(&LastCounter, TargetSecondsPerFrame, SleepIsGranular);

#if ENGINE_INTERNAL
        Win32DebugSyncDisplay(&GlobalBackBuffer, ArrayCount(DebugTimeMarkers), DebugTimeMarkers, DebugTimeMarkerIndex - 1, &SoundOutput, TargetSecondsPerFrame);
#endif
        RenderVulkanFrame(&RenderCommands);
        if (GiOptions.FrameLimit)
        {
            uint64 RenderedFrames = GlobalRenderer.Context.frameIndex - FirstGiFrame;
            if (RenderedFrames >= GiOptions.FrameLimit) isRunning = false;
            if (GlobalRenderer.Context.frameIndex == LastGiFrame) ++GiStalledFrames;
            else GiStalledFrames = 0;
            LastGiFrame = GlobalRenderer.Context.frameIndex;
            if (GiStalledFrames >= 300)
            {
                DebugLog("GI validation failed: renderer made no progress for 300 attempts\n");
                ExitCode = 3;
                isRunning = false;
            }
        }
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

    if (GiOptions.Enabled)
    {
        // Wait for submitted GPU work before the test reports completion.
        if (vkDeviceWaitIdle(GlobalRenderer.Context.device) != VK_SUCCESS) ExitCode = 3;
        uint64 RenderedFrames = GlobalRenderer.Context.frameIndex - FirstGiFrame;
        if (GiOptions.FrameLimit && RenderedFrames < GiOptions.FrameLimit) ExitCode = 3;
        DebugLog("GI validation finished: scene=%u view=%u submitted_frames=%llu target=%u exit=%d\n", GiOptions.Scene, GiOptions.View, RenderedFrames, GiOptions.FrameLimit, ExitCode);
        Win32UnloadGameCode(&Game);
        DeleteFileA(Paths.TempGameCodeDLLFullPath);
        if (GlobalSecondaryBuffer) GlobalSecondaryBuffer->Stop();
        ReleaseDC(Window, DeviceContext);
        DestroyWindow(Window);
    }
    return ExitCode;
}
