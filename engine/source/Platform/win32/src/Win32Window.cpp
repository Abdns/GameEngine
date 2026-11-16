#include <stdint.h>

bool32 isRunning = true;

static WNDCLASS CreateWindowClass(UINT style, WNDPROC mainWindowCallback, HINSTANCE instance, LPCWSTR className)
{
    WNDCLASS WindowsClass = { 0 };
    WindowsClass.style         = style;
    WindowsClass.lpfnWndProc   = mainWindowCallback;
    WindowsClass.hInstance     = instance;
    WindowsClass.lpszClassName = className;
    return WindowsClass;
}

LRESULT CALLBACK Win32MainWindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;

    switch (Message)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);

            EndPaint(Window, &Paint);
        } break;

        case WM_SIZE:
        {
        } break;

        case WM_DESTROY:
        {
            isRunning = false;
            OutputDebugStringA("WM_DESTROY\n");
        } break;

        case WM_CLOSE:
        {
            isRunning = false;
            OutputDebugStringA("WM_CLOSE\n");
        } break;

        case WM_ACTIVATEAPP:
        {
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            Assert(!"Keyboard message reached WindowProc");
        } break;

        default:
        {
            Result = DefWindowProc(Window, Message, WParam, LParam);
        } break;
    }

    return Result;
}

HWND Win32CreateMainWindow(HINSTANCE Instance, LPCWSTR ClassName, LPCWSTR Title, int Width, int Height)
{
    WNDCLASS WindowsClass = CreateWindowClass(CS_HREDRAW | CS_VREDRAW, Win32MainWindowCallback, Instance, ClassName);
    RegisterClass(&WindowsClass);

    return CreateWindowEx(0, ClassName, Title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, Width, Height,0, 0, Instance, 0);
}

void Win32ToggleFullscreen(HWND Window)
{
    static WINDOWPLACEMENT GlobalWindowPosition = { sizeof(GlobalWindowPosition) };

    DWORD Style = GetWindowLong(Window, GWL_STYLE);
    if (Style & WS_OVERLAPPEDWINDOW)
    {
        MONITORINFO MonitorInfo = { sizeof(MonitorInfo) };
        if (GetWindowPlacement(Window, &GlobalWindowPosition) &&
            GetMonitorInfo(MonitorFromWindow(Window, MONITOR_DEFAULTTOPRIMARY), &MonitorInfo))
        {
            SetWindowLong(Window, GWL_STYLE, Style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(Window, HWND_TOP,
                         MonitorInfo.rcMonitor.left,  MonitorInfo.rcMonitor.top,
                         MonitorInfo.rcMonitor.right  - MonitorInfo.rcMonitor.left,
                         MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    }
    else
    {
        SetWindowLong(Window, GWL_STYLE, Style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(Window, &GlobalWindowPosition);
        SetWindowPos(Window, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

internal void Win32ProcessKeyboardMessage(platform_button_state* NewState, bool32 IsDown)
{
    if (NewState->EndedDown != IsDown)
    {
        NewState->EndedDown = IsDown;
        ++NewState->HalfTransitionCount;
    }
}

void Win32ProcessPendingMessages(win32_state* State, platform_input* Input)
{
    MSG Message;
    while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
    {
        switch (Message.message)
        {
            case WM_QUIT:
            {
                isRunning = false;
            } break;

            case WM_MOUSEWHEEL:
            {
                Input->MouseZ += GET_WHEEL_DELTA_WPARAM(Message.wParam);
            } break;

            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            {
                uint32 VKCode = (uint32)Message.wParam;
                bool32 WasDown = (Message.lParam & (1 << 30)) != 0;
                bool32 IsDown  = (Message.lParam & (1 << 31)) == 0;

                if (WasDown != IsDown)
                {
                    if (VKCode < ArrayCount(Input->Keys))
                    {
                        Win32ProcessKeyboardMessage(&Input->Keys[VKCode], IsDown);
                    }
                    if (VKCode == VK_ESCAPE)
                    {
                        isRunning = false;
                    }
#if ENGINE_INTERNAL
                    else if (VKCode == 'L' && IsDown)
                    {

                        if (State->InputPlayingIndex == 0)
                        {
                            if (State->InputRecordingIndex == 0)
                            {
                                Win32BeginRecordingInput(State, 1);
                            }
                            else
                            {
                                Win32EndRecordingInput(State);
                                Win32BeginInputPlayBack(State, 1);
                            }
                        }
                        else
                        {
                            Win32EndInputPlayBack(State);
                        }
                    }
#endif
                }

                bool32 AltKeyWasDown = (Message.lParam & (1 << 29)) != 0;
                if ((VKCode == VK_F4) && AltKeyWasDown)
                {
                    isRunning = false;
                }

                if (IsDown && (VKCode == VK_F11 || (VKCode == VK_RETURN && AltKeyWasDown)))
                {
                    HWND Window = GetActiveWindow();
                    if (Window) Win32ToggleFullscreen(Window);
                }
            } break;

            default:
            {
                TranslateMessage(&Message);
                DispatchMessage(&Message);
            } break;
        }
    }
}
