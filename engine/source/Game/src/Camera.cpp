#include "Types.h"
#include "EngineMath.h"
#include "PlatformAPI.h"

struct camera
{
    world_position Position;
    real32         Yaw;
    real32         Pitch;
    real32         FovY;
};

struct camera_basis
{
    Vector3 Right;
    Vector3 Up;
    Vector3 Forward;
};

internal void InitCamera(camera *Camera, world_position P, real32 FovY)
{
    Camera->Position     = P;
    Camera->Yaw   = 0.0f;
    Camera->Pitch = 0.0f;
    Camera->FovY  = FovY;
}

internal camera_basis GetCameraBasis(camera *Camera)
{
    real32 cy = Cos(Camera->Yaw);
    real32 sy = Sin(Camera->Yaw);
    real32 cp = Cos(Camera->Pitch);
    real32 sp = Sin(Camera->Pitch);

    camera_basis Result;
    Result.Forward = Vector3(-sy * cp, sp, -cy * cp);
    Result.Right   = Vector3(cy, 0.0f, -sy);
    Result.Up      = Cross(Result.Right, Result.Forward);

    return Result;
}

internal Matrix4 CameraView(camera *Camera, Vector3 SimSpaceP)
{
    return Mat4Multiply(Mat4RotationX(-Camera->Pitch),
                        Mat4Multiply(Mat4RotationY(-Camera->Yaw),
                                     Mat4Translation(-SimSpaceP.X, -SimSpaceP.Y, -SimSpaceP.Z)));
}

internal ray CameraRayFromScreen(camera *Camera, Vector3 SimSpaceP, real32 MouseX, real32 MouseY, real32 ViewportWidth, real32 ViewportHeight)
{
    camera_basis Basis = GetCameraBasis(Camera);

    return RayFromScreen(MouseX, MouseY, ViewportWidth, ViewportHeight, Camera->FovY, SimSpaceP, Basis.Right, Basis.Up, Basis.Forward);
}

internal void UpdateCamera(camera *Camera, world *World, game_input *Input)
{
    real32 dMouseX = (real32)(Input->MouseX - Input->LastMouseX);
    real32 dMouseY = (real32)(Input->MouseY - Input->LastMouseY);

    if (Input->MouseButtons[2].EndedDown)
    {
        real32 Sensitivity = 0.003f;
        Camera->Yaw   -= dMouseX * Sensitivity;
        Camera->Pitch -= dMouseY * Sensitivity;
        Camera->Pitch = Clamp(-1.55f, Camera->Pitch, 1.55f);
    }

    camera_basis Basis = GetCameraBasis(Camera);
    Vector3 Forward = Basis.Forward;
    Vector3 Right   = Basis.Right;
    Vector3 WorldUp = Vector3(0.0f, 1.0f, 0.0f);

    game_controller_input *Keyboard = &Input->Controllers[0];
    Vector3 Move = Vector3(0.0f, 0.0f, 0.0f);
    if (Keyboard->Up.EndedDown)            Move += Forward;
    if (Keyboard->Down.EndedDown)          Move -= Forward;
    if (Keyboard->Right.EndedDown)         Move += Right;
    if (Keyboard->Left.EndedDown)          Move -= Right;
    if (Keyboard->RightShoulder.EndedDown) Move += WorldUp;
    if (Keyboard->LeftShoulder.EndedDown)  Move -= WorldUp;

    real32 Speed = 4.0f;

    Camera->Position = MapIntoChunkSpace(World, Camera->Position, (Speed * Input->dtForFrame) * Move);
}
