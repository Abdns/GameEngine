#ifndef INPUT_H
#define INPUT_H

#include "Types.h"
#include "EngineMath.h"

struct mouse_input
{
    Vector2 Position;
    Vector2 Delta;
    int32   Wheel;
    bool32  Down;
    bool32  Pressed;
    bool32  Released;
    bool32  MiddleDown;
    bool32  MiddlePressed;
    bool32  LookDown;

    bool32 Consumed;
    bool32 OverUI;
};

struct input_state
{
    mouse_input Mouse;

    Vector3 MoveAxis;

    real32  dtForFrame;
    Vector2 ViewportSize;
};

inline bool32 MouseAvailable(mouse_input *Mouse)
{
    return !Mouse->Consumed && !Mouse->OverUI;
}

#endif
