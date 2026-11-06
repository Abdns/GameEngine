#define WIN32_DEFAULT_DPI 96.0f

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
#define XInputGetState XInputGetState_

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
#define XInputSetState XInputSetState_

X_INPUT_GET_STATE(XInputGetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_get_state* XInputGetState_ = XInputGetStateStub;

X_INPUT_SET_STATE(XInputSetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_set_state* XInputSetState_ = XInputSetStateStub;

void Win32LoadXInput(void)
{
    HMODULE XInputLibrary = LoadLibrary(L"xinput1_4.dll");
    if (!XInputLibrary)
    {
        XInputLibrary = LoadLibrary(L"xinput9_1_0.dll");
    }
    if (!XInputLibrary)
    {
        XInputLibrary = LoadLibrary(L"xinput1_3.dll");
    }
    if (XInputLibrary)
    {
        XInputGetState = (x_input_get_state*)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state*)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

void Win32ProcessXInput(platform_input* Input)
{
    Assert(XUSER_MAX_COUNT <= ArrayCount(Input->Gamepads));
    for (DWORD Index = 0; Index < XUSER_MAX_COUNT; ++Index)
    {
        platform_gamepad_input* Controller = &Input->Gamepads[Index];
        *Controller = {};

        XINPUT_STATE State;
        if (XInputGetState(Index, &State) == ERROR_SUCCESS)
        {
            XINPUT_GAMEPAD* Pad = &State.Gamepad;
            Controller->IsConnected = true;
            Controller->Buttons = Pad->wButtons;
            Controller->LeftStickX = Pad->sThumbLX;
            Controller->LeftStickY = Pad->sThumbLY;
            Controller->RightStickX = Pad->sThumbRX;
            Controller->RightStickY = Pad->sThumbRY;
            Controller->LeftTrigger = Pad->bLeftTrigger;
            Controller->RightTrigger = Pad->bRightTrigger;
        }
    }
}

void Win32PrepareKeyboardInput(platform_input* NewInput, platform_input* OldInput)
{
    for (uint32 Index = 0; Index < ArrayCount(NewInput->Keys); ++Index)
    {
        NewInput->Keys[Index].EndedDown = OldInput->Keys[Index].EndedDown;
        NewInput->Keys[Index].HalfTransitionCount = 0;
    }
}

internal void Win32ProcessMouseButton(platform_button_state* NewState, platform_button_state* OldState, bool32 IsDown)
{
    NewState->EndedDown = IsDown;
    NewState->HalfTransitionCount = (OldState->EndedDown != IsDown) ? 1 : 0;
}

internal void Win32GetMousePosition(HWND Window, int32* MouseX, int32* MouseY)
{
    POINT MouseP;
    GetCursorPos(&MouseP);
    ScreenToClient(Window, &MouseP);

    *MouseX = MouseP.x;
    *MouseY = MouseP.y;
}

void Win32PrimeMouseInput(HWND Window, platform_input* NewInput, platform_input* OldInput)
{
    Win32GetMousePosition(Window, &OldInput->MouseX, &OldInput->MouseY);
    OldInput->LastMouseX = OldInput->MouseX;
    OldInput->LastMouseY = OldInput->MouseY;

    NewInput->MouseX = NewInput->LastMouseX = OldInput->MouseX;
    NewInput->MouseY = NewInput->LastMouseY = OldInput->MouseY;
}

void Win32ProcessMouseInput(HWND Window, platform_input* NewInput, platform_input* OldInput)
{
    Win32GetMousePosition(Window, &NewInput->MouseX, &NewInput->MouseY);

    NewInput->LastMouseX = OldInput->MouseX;
    NewInput->LastMouseY = OldInput->MouseY;

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    NewInput->RenderWidth  = ClientRect.right - ClientRect.left;
    NewInput->RenderHeight = ClientRect.bottom - ClientRect.top;
    NewInput->UIScale      = (real32)GetDpiForWindow(Window) / WIN32_DEFAULT_DPI;

    Win32ProcessMouseButton(&NewInput->MouseButtons[0], &OldInput->MouseButtons[0],
                            GetKeyState(VK_LBUTTON) & (1 << 15));
    Win32ProcessMouseButton(&NewInput->MouseButtons[1], &OldInput->MouseButtons[1],
                            GetKeyState(VK_MBUTTON) & (1 << 15));
    Win32ProcessMouseButton(&NewInput->MouseButtons[2], &OldInput->MouseButtons[2],
                            GetKeyState(VK_RBUTTON) & (1 << 15));
    Win32ProcessMouseButton(&NewInput->MouseButtons[3], &OldInput->MouseButtons[3],
                            GetKeyState(VK_XBUTTON1) & (1 << 15));
    Win32ProcessMouseButton(&NewInput->MouseButtons[4], &OldInput->MouseButtons[4],
                            GetKeyState(VK_XBUTTON2) & (1 << 15));
}

void Win32ProcessInput(platform_input* NewInput, platform_input* OldInput,
                      win32_state* State, HWND Window)
{
    Win32PrepareKeyboardInput(NewInput, OldInput);
    NewInput->MouseZ = 0;
    Win32ProcessPendingMessages(State, NewInput);
    Win32ProcessMouseInput(Window, NewInput, OldInput);
    Win32ProcessXInput(NewInput);
}
