#include "Types.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "Input.h"

internal void BeginInput(input_state *State, game_input *Input)
{
    game_button_state *Select = &Input->MouseButtons[0];
    game_button_state *Middle = &Input->MouseButtons[1];
    game_button_state *Look   = &Input->MouseButtons[2];

    mouse_input *Mouse = &State->Mouse;

    Mouse->Position      = Vector2((real32)Input->MouseX, (real32)Input->MouseY);
    Mouse->Delta         = Vector2((real32)(Input->MouseX - Input->LastMouseX), (real32)(Input->MouseY - Input->LastMouseY));
    Mouse->Wheel         = Input->MouseZ;
    Mouse->Down          = Select->EndedDown;
    Mouse->Pressed       = (Select->EndedDown && Select->HalfTransitionCount);
    Mouse->Released      = (!Select->EndedDown && Select->HalfTransitionCount);
    Mouse->MiddleDown    = Middle->EndedDown;
    Mouse->MiddlePressed = (Middle->EndedDown && Middle->HalfTransitionCount);
    Mouse->LookDown      = Look->EndedDown;
    Mouse->Consumed      = false;
    Mouse->OverUI        = false;

    game_controller_input *Keyboard = &Input->Controllers[0];

    State->MoveAxis = Vector3(0.0f, 0.0f, 0.0f);
    if (Keyboard->Right.EndedDown)         State->MoveAxis.X += 1.0f;
    if (Keyboard->Left.EndedDown)          State->MoveAxis.X -= 1.0f;
    if (Keyboard->RightShoulder.EndedDown) State->MoveAxis.Y += 1.0f;
    if (Keyboard->LeftShoulder.EndedDown)  State->MoveAxis.Y -= 1.0f;
    if (Keyboard->Up.EndedDown)            State->MoveAxis.Z += 1.0f;
    if (Keyboard->Down.EndedDown)          State->MoveAxis.Z -= 1.0f;

    State->dtForFrame   = Input->dtForFrame;
    State->ViewportSize = Vector2((real32)Input->RenderWidth, (real32)Input->RenderHeight);
}
