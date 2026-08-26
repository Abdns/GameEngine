#include "Types.h"
#include "EngineMath.h"
#include "Input.h"
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

internal void UpdateCamera(camera *Camera, world *World, input_state *Controls)
{
    if (Controls->Mouse.LookDown)
    {
        real32 Sensitivity = 0.003f;
        Camera->Yaw   -= Controls->Mouse.Delta.X * Sensitivity;
        Camera->Pitch -= Controls->Mouse.Delta.Y * Sensitivity;
        Camera->Pitch = Clamp(-1.55f, Camera->Pitch, 1.55f);
    }

    camera_basis Basis = GetCameraBasis(Camera);
    Vector3 WorldUp = Vector3(0.0f, 1.0f, 0.0f);

    Vector3 Move = Controls->MoveAxis.X * Basis.Right +
                   Controls->MoveAxis.Y * WorldUp +
                   Controls->MoveAxis.Z * Basis.Forward;

    real32 Speed = 4.0f;

    Camera->Position = MapIntoChunkSpace(World, Camera->Position, (Speed * Controls->dtForFrame) * Move);
}
