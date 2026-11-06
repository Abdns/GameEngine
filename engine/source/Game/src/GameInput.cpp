#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "PlatformAPI.h"

// Mapped pointer actions shared by the game, editor, and UI for this frame.
struct game_mouse_input
{
    Vector2 Position;
    Vector2 Delta;
    int32   Wheel;
    bool32  Down;
    bool32  Pressed;
    bool32  Released;
    bool32  LookDown;

    bool32 Consumed;
    bool32 OverUI;
};

struct game_input
{
    game_mouse_input Mouse;

    Vector3 MoveAxis;

    real32  dtForFrame;
    Vector2 ViewportSize;
    real32  UIScale;
};

internal bool32 MouseAvailable(const game_mouse_input *Mouse)
{
    return !Mouse->Consumed && !Mouse->OverUI;
}

// All device-to-game bindings live here. Platform code only reports devices.
internal void BeginGameInput(game_input *State, const platform_input *Input)
{
    const platform_button_state *Select = &Input->MouseButtons[0];
    const platform_button_state *Look   = &Input->MouseButtons[2];

    game_mouse_input *Mouse = &State->Mouse;
    Mouse->Position = Vector2((real32)Input->MouseX, (real32)Input->MouseY);
    Mouse->Delta    = Vector2((real32)(Input->MouseX - Input->LastMouseX), (real32)(Input->MouseY - Input->LastMouseY));
    Mouse->Wheel    = Input->MouseZ;
    Mouse->Down     = Select->EndedDown;
    Mouse->Pressed  = Select->EndedDown && Select->HalfTransitionCount;
    Mouse->Released = !Select->EndedDown && Select->HalfTransitionCount;
    Mouse->LookDown = Look->EndedDown;
    Mouse->Consumed = false;
    Mouse->OverUI   = false;

    const platform_button_state *Keys = Input->Keys;
    State->MoveAxis = Vector3(0.0f, 0.0f, 0.0f);
    if (Keys['D'].EndedDown || Keys[Key_Right].EndedDown) State->MoveAxis.X += 1.0f;
    if (Keys['A'].EndedDown || Keys[Key_Left].EndedDown)  State->MoveAxis.X -= 1.0f;
    if (Keys['E'].EndedDown)                           State->MoveAxis.Y += 1.0f;
    if (Keys['Q'].EndedDown)                           State->MoveAxis.Y -= 1.0f;
    if (Keys['W'].EndedDown || Keys[Key_Up].EndedDown)    State->MoveAxis.Z += 1.0f;
    if (Keys['S'].EndedDown || Keys[Key_Down].EndedDown)  State->MoveAxis.Z -= 1.0f;

    State->dtForFrame   = Input->dtForFrame;
    State->ViewportSize = Vector2((real32)Input->RenderWidth, (real32)Input->RenderHeight);
    State->UIScale      = Input->UIScale;
}
