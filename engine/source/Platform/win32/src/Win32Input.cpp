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

internal void Win32ProcessXInputDigitalButton(DWORD XInputButtonState,
                                              game_button_state* OldState, DWORD ButtonBit,
                                              game_button_state* NewState)
{
    NewState->EndedDown = ((XInputButtonState & ButtonBit) == ButtonBit);
    NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

internal real32 Win32ProcessXInputStickValue(SHORT Value, SHORT DeadZoneThreshold)
{
    real32 Result = 0.0f;
    if (Value < -DeadZoneThreshold)
    {
        Result = (real32)(Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold);
    }
    else if (Value > DeadZoneThreshold)
    {
        Result = (real32)(Value - DeadZoneThreshold) / (32767.0f - DeadZoneThreshold);
    }
    return Result;
}

void Win32ProcessXInput(game_input* NewInput, game_input* OldInput)
{
    DWORD MaxControllerCount = XUSER_MAX_COUNT;
    Assert(MaxControllerCount <= (ArrayCount(NewInput->Controllers) - 1));

    for (DWORD ControllerIndex = 0; ControllerIndex < MaxControllerCount; ++ControllerIndex)
    {
        DWORD OurControllerIndex = ControllerIndex + 1;
        game_controller_input* OldController = &OldInput->Controllers[OurControllerIndex];
        game_controller_input* NewController = &NewInput->Controllers[OurControllerIndex];

        XINPUT_STATE ControllerState;
        if (XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
        {
            XINPUT_GAMEPAD* Pad = &ControllerState.Gamepad;

            NewController->IsConnected = true;
            NewController->IsAnalog = true;

            NewController->StickAverageX =
                Win32ProcessXInputStickValue(Pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            NewController->StickAverageY =
                Win32ProcessXInputStickValue(Pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

            Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->Up,
                                            XINPUT_GAMEPAD_DPAD_UP, &NewController->Up);
            Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->Down,
                                            XINPUT_GAMEPAD_DPAD_DOWN, &NewController->Down);
            Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->Left,
                                            XINPUT_GAMEPAD_DPAD_LEFT, &NewController->Left);
            Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->Right,
                                            XINPUT_GAMEPAD_DPAD_RIGHT, &NewController->Right);
            Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->LeftShoulder,
                                            XINPUT_GAMEPAD_LEFT_SHOULDER, &NewController->LeftShoulder);
            Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->RightShoulder,
                                            XINPUT_GAMEPAD_RIGHT_SHOULDER, &NewController->RightShoulder);
        }
        else
        {
            NewController->IsConnected = false;
        }
    }
}

void Win32PrepareKeyboardInput(game_input* NewInput, game_input* OldInput)
{
    game_controller_input* OldKb = &OldInput->Controllers[0];
    game_controller_input* NewKb = &NewInput->Controllers[0];
    *NewKb = {};
    NewKb->IsConnected = true;
    for (int i = 0; i < ArrayCount(NewKb->Buttons); ++i)
    {
        NewKb->Buttons[i].EndedDown = OldKb->Buttons[i].EndedDown;
    }
}

internal void Win32ProcessMouseButton(game_button_state* NewState, game_button_state* OldState, bool32 IsDown)
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

void Win32PrimeMouseInput(HWND Window, game_input* NewInput, game_input* OldInput)
{
    Win32GetMousePosition(Window, &OldInput->MouseX, &OldInput->MouseY);
    OldInput->LastMouseX = OldInput->MouseX;
    OldInput->LastMouseY = OldInput->MouseY;

    NewInput->MouseX = NewInput->LastMouseX = OldInput->MouseX;
    NewInput->MouseY = NewInput->LastMouseY = OldInput->MouseY;
}

void Win32ProcessMouseInput(HWND Window, game_input* NewInput, game_input* OldInput)
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

void Win32ProcessInput(game_input* NewInput, game_input* OldInput,
                      win32_state* State, HWND Window)
{
    Win32PrepareKeyboardInput(NewInput, OldInput);
    NewInput->MouseZ = 0;
    Win32ProcessPendingMessages(State, NewInput);
    Win32ProcessMouseInput(Window, NewInput, OldInput);
    Win32ProcessXInput(NewInput, OldInput);
}
