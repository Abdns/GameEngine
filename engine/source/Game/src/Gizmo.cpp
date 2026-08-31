#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "Input.h"
#include "AssetStore.cpp"
#include "SimEntity.cpp"
#include "Raycast.cpp"

#define GIZMO_AXIS_COUNT 3

struct gizmo_style
{
    Vector4 Base;
    Vector4 Hot;
    Vector4 Active;
    Vector4 Selected;

    uint32  AxisMesh[GIZMO_AXIS_COUNT];
    Vector4 AxisColor[GIZMO_AXIS_COUNT];

    uint32 Material;
};

struct gizmo_context
{
    mouse_input *Mouse;

    uint32 Hot;
    uint32 Active;
    uint32 Selected;

    Vector3 GrabOffset;

    gizmo_style      Style;
    render_commands *Commands;
};

internal gizmo_style DefaultGizmoStyle(uint32 *AxisMeshes, uint32 Material)
{
    gizmo_style Style = {};

    Style.Base     = Vector4(0.16f, 0.18f, 0.24f, 0.85f);
    Style.Hot      = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    Style.Active   = Vector4(0.15f, 0.45f, 0.90f, 0.95f);
    Style.Selected = Vector4(1.0f, 0.85f, 0.2f, 1.0f);

    Style.AxisColor[0] = Vector4(0.90f, 0.20f, 0.20f, 1.0f);
    Style.AxisColor[1] = Vector4(0.30f, 0.85f, 0.30f, 1.0f);
    Style.AxisColor[2] = Vector4(0.25f, 0.45f, 0.95f, 1.0f);

    for (uint32 Axis = 0; Axis < GIZMO_AXIS_COUNT; ++Axis)
    {
        Style.AxisMesh[Axis] = AxisMeshes[Axis];
    }

    Style.Material = Material;

    return Style;
}

internal void BeginGizmo(gizmo_context *Gizmo, mouse_input *Mouse, render_commands *Commands)
{
    Gizmo->Mouse    = Mouse;
    Gizmo->Hot      = 0;
    Gizmo->Commands = Commands;
}

internal void EndGizmo(gizmo_context *Gizmo)
{
    if (!Gizmo->Mouse->Down)
    {
        Gizmo->Active = 0;
    }
}

internal void GizmoSelect(gizmo_context *Gizmo, uint32 StorageIndex)
{
    if (Gizmo->Selected == ENTITY_STORAGE_NONE && StorageIndex != ENTITY_STORAGE_NONE)
    {
        Gizmo->Selected = StorageIndex;
    }
    else if (Gizmo->Selected != ENTITY_STORAGE_NONE)
    {
        Gizmo->Selected = ENTITY_STORAGE_NONE;
    }
}

internal Vector4 GizmoAxisColor(gizmo_context *Gizmo, uint32 ID, uint32 AxisIndex)
{
    if (Gizmo->Active == ID)
    {
        return Gizmo->Style.Active;
    }

    if (Gizmo->Hot == ID)
    {
        return Gizmo->Style.Hot;
    }

    return Gizmo->Style.AxisColor[AxisIndex];
}

internal Vector3 GizmoAxisDirection(uint32 AxisIndex)
{
    Vector3 Result = Vector3(0.0f, 0.0f, 0.0f);
    Result.Elements[AxisIndex] = 1.0f;

    return Result;
}

internal bool32 ClosestPointOnAxis(Vector3 Origin, Vector3 Direction, ray Ray, Vector3 *OutPoint)
{
    Vector3 ToOrigin = Origin - Ray.Origin;

    real32 Along = Dot(Direction, Ray.Direction);
    real32 Denom = 1.0f - Along * Along;

    if (Denom < 1e-4f)
    {
        return false;
    }

    real32 OnAxis = Dot(Direction, ToOrigin);
    real32 OnRay  = Dot(Ray.Direction, ToOrigin);

    *OutPoint = Origin + ((Along * OnRay - OnAxis) / Denom) * Direction;

    return true;
}

internal bool32 GizmoRayHitsAxis(gizmo_context *Gizmo, asset_store *Assets, uint32 AxisIndex, Matrix4 Transform, ray PickRay)
{
    Matrix4 ToLocal = Mat4InverseRigid(Transform);

    ray LocalRay;
    LocalRay.Origin    = Mat4Transform(ToLocal, PickRay.Origin, 1.0f);
    LocalRay.Direction = Mat4Transform(ToLocal, PickRay.Direction, 0.0f);

    collision Mesh = AssetCollisionMesh(Assets, Gizmo->Style.AxisMesh[AxisIndex]);

    real32 Distance;
    return RayIntersectsMesh(&Mesh, LocalRay, &Distance);
}

internal bool32 Axis(gizmo_context *Gizmo, asset_store *Assets, ray PickRay, Vector3 *Position)
{
    if (Gizmo->Selected == ENTITY_STORAGE_NONE)
    {
        return false;
    }

    Matrix4 Transform = Mat4Rigid(*Position, QuatIdentity());

    bool32 Moved = false;

    for (uint32 AxisIndex = 0; AxisIndex < GIZMO_AXIS_COUNT; ++AxisIndex)
    {
        uint32  ID        = AxisIndex + 1;
        Vector3 Direction = GizmoAxisDirection(AxisIndex);

        if (Gizmo->Active == ID)
        {
            Gizmo->Mouse->Consumed = true;

            Vector3 OnAxis;
            if (ClosestPointOnAxis(*Position, Direction, PickRay, &OnAxis))
            {
                *Position = OnAxis + Gizmo->GrabOffset;
                Moved     = true;
            }

            if (Gizmo->Mouse->Released)
            {
                Gizmo->Active = 0;
            }
        }
        else if (!Gizmo->Active && !Gizmo->Mouse->Consumed && GizmoRayHitsAxis(Gizmo, Assets, AxisIndex, Transform, PickRay))
        {
            Gizmo->Hot             = ID;
            Gizmo->Mouse->Consumed = true;

            if (Gizmo->Mouse->Pressed)
            {
                Vector3 OnAxis;
                if (ClosestPointOnAxis(*Position, Direction, PickRay, &OnAxis))
                {
                    Gizmo->Active     = ID;
                    Gizmo->GrabOffset = *Position - OnAxis;
                }
            }
        }

        PushRenderMesh(Gizmo->Commands, Transform, GizmoAxisColor(Gizmo, ID, AxisIndex), Gizmo->Style.AxisMesh[AxisIndex], Gizmo->Style.Material);
    }

    return Moved;
}
