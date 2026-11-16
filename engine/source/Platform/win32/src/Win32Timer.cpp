int64 GlobalPerfCountFrequency;

void Win32InitTimer(bool32* SleepIsGranular)
{
    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency(&PerfCountFrequencyResult);
    GlobalPerfCountFrequency = PerfCountFrequencyResult.QuadPart;

    UINT DesiredSchedulerMS = 1;
    *SleepIsGranular = (timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR);
}

int32 Win32GetMonitorRefreshHz(HDC DeviceContext)
{
    int32 MonitorRefreshHz = 60;
    int32 Win32RefreshRate = GetDeviceCaps(DeviceContext, VREFRESH);
    if (Win32RefreshRate > 1) MonitorRefreshHz = Win32RefreshRate;
    return MonitorRefreshHz;
}

real32 Win32WaitForFrameEnd(LARGE_INTEGER* LastCounter,
                            real32 TargetSecondsPerFrame,
                            bool32 SleepIsGranular)
{
    LARGE_INTEGER WorkCounter = Win32GetWallClock();
    real32 SecondsElapsedForFrame = Win32GetSecondsElapsed(*LastCounter, WorkCounter);

    if (SecondsElapsedForFrame < TargetSecondsPerFrame)
    {
        if (SleepIsGranular)
        {
            DWORD SleepMS = (DWORD)(1000.0f * (TargetSecondsPerFrame - SecondsElapsedForFrame));
            if (SleepMS > 0) Sleep(SleepMS);
        }

        real32 TestSecondsElapsedForFrame = Win32GetSecondsElapsed(*LastCounter, Win32GetWallClock());
        while (TestSecondsElapsedForFrame < TargetSecondsPerFrame)
        {
            TestSecondsElapsedForFrame = Win32GetSecondsElapsed(*LastCounter, Win32GetWallClock());
        }
    }

    LARGE_INTEGER EndCounter = Win32GetWallClock();
    real32 MSPerFrame = 1000.0f * Win32GetSecondsElapsed(*LastCounter, EndCounter);
    *LastCounter = EndCounter;
    return MSPerFrame;
}

void Win32OutputFrameStats(real32 MSPerFrame, int64 CycleElapsed)
{
    real32 FPS  = 1000.0f / MSPerFrame;
    real32 MCPF = (real32)CycleElapsed / (1000.0f * 1000.0f);

    char TextBuffer[256];
    wsprintfA(TextBuffer, "%dms/f, %dF/s, %dmc/f\n",
              (int)MSPerFrame, (int)FPS, (int)MCPF);
    OutputDebugStringA(TextBuffer);
}
