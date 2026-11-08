#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "GameInput.cpp"
#include "PlatformAPI.h"
#include "World.cpp"

struct camera
{
    world_position Position;
    Vector3        RotationDegrees;
    real32         FovY;
};

struct camera_basis
{
    Vector3 Right;
    Vector3 Up;
    Vector3 Forward;
};

internal void InitCamera(camera *Camera, world_position Position, Vector3 RotationDegrees, real32 FovY)
{
    Camera->Position = Position;
    Camera->RotationDegrees = RotationDegrees;
    Camera->FovY  = FovY;
}

internal Matrix4 CameraRotation(camera *Camera)
{
    // World Y, then local X, then local Z. Angles exposed to callers are degrees.
    Vector3 R = Camera->RotationDegrees;
    return Mat4Multiply(Mat4RotationY(DegToRad(R.Y)),
                        Mat4Multiply(Mat4RotationX(DegToRad(R.X)), Mat4RotationZ(DegToRad(R.Z))));
}

internal camera_basis GetCameraBasis(camera *Camera)
{
    Matrix4 Rotation = CameraRotation(Camera);
    camera_basis Basis;
    Basis.Right   = Mat4Transform(Rotation, Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    Basis.Up      = Mat4Transform(Rotation, Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    Basis.Forward = Mat4Transform(Rotation, Vector3(0.0f, 0.0f, -1.0f), 0.0f);

    return Basis;
}

internal Matrix4 CameraView(camera *Camera, Vector3 SimSpaceP)
{
    Matrix4 Transform = CameraRotation(Camera);
    Transform.Elements[3][0] = SimSpaceP.X;
    Transform.Elements[3][1] = SimSpaceP.Y;
    Transform.Elements[3][2] = SimSpaceP.Z;
    return Mat4InverseRigid(Transform);
}

internal ray CameraRayFromScreen(camera *Camera, Vector3 SimSpaceP, real32 MouseX, real32 MouseY, real32 ViewportWidth, real32 ViewportHeight)
{
    camera_basis Basis = GetCameraBasis(Camera);

    return RayFromScreen(MouseX, MouseY, ViewportWidth, ViewportHeight, Camera->FovY, SimSpaceP, Basis.Right, Basis.Up, Basis.Forward);
}

internal void UpdateCamera(camera *Camera, world *World, game_input *Controls)
{
    if (IsDebugMode(Controls) && Controls->Mouse.LookDown)
    {
        real32 Sensitivity = 0.003f * (180.0f / Pi32);
        Camera->RotationDegrees.Y -= Controls->Mouse.Delta.X * Sensitivity;
        Camera->RotationDegrees.X -= Controls->Mouse.Delta.Y * Sensitivity;
        Camera->RotationDegrees.X = Clamp(-89.0f, Camera->RotationDegrees.X, 89.0f);

        camera_basis Basis = GetCameraBasis(Camera);
        Vector3 WorldUp = Vector3(0.0f, 1.0f, 0.0f);

        Vector3 Move = Controls->MoveAxis.X * Basis.Right + Controls->MoveAxis.Y * WorldUp + Controls->MoveAxis.Z * Basis.Forward;
        real32 Speed = 4.0f;

        Camera->Position = MapIntoChunkSpace(World, Camera->Position, (Speed * Controls->dtForFrame) * Move);
    }
}
