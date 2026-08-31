#pragma once

#include "Types.h"
#include "EngineMath.h"
#define ENTITY_STORAGE_NONE 0

enum entity_type
{
    Entity_Null = 0,
    Entity_Floor,
    Entity_Prop,
    Entity_Ball,
};

enum entity_flag
{
    EntityFlag_Visible   = 0x1,
    EntityFlag_Simulates = 0x2,
    EntityFlag_Static    = 0x4,
};

struct sim_entity
{
    uint32      LowStorageIndex;

    entity_type Type;
    uint32      Flags;

    transform   Current;
    transform   Previous;

    Vector3     dPosition;
    Vector3     dOrientation;

    real32      InvMass;
    real32      InvInertia;

    uint32      MeshHandle;
    uint32      MaterialHandle;
    Vector4     Tint;

    rectangle3  Bounds;

    bool32      Updatable;
};

union sim_entity_reference
{
    sim_entity *Ptr;
    uint32      Index;
};

internal void SimEntitySetPosition(sim_entity *Entity, Vector3 Position)
{
    Entity->Current.Position  = Position;
    Entity->Previous.Position = Position;
    Entity->dPosition         = Vector3(0.0f, 0.0f, 0.0f);
}

internal void SimEntitySetOrientation(sim_entity *Entity, Quaternion Orientation)
{
    Entity->Current.Orientation  = Orientation;
    Entity->Previous.Orientation = Orientation;
    Entity->dOrientation         = Vector3(0.0f, 0.0f, 0.0f);
}

internal void SimEntitySetScale(sim_entity *Entity, Vector3 Scale)
{
    Entity->Current.Scale  = Scale;
    Entity->Previous.Scale = Scale;
}

internal transform SimEntityRenderPose(sim_entity *Entity, real32 Alpha)
{
    return TransformLerp(Entity->Previous, Entity->Current, Alpha);
}

internal Vector3 SimEntityRenderPosition(sim_entity *Entity, real32 Alpha)
{
    return SimEntityRenderPose(Entity, Alpha).Position;
}

internal Matrix4 SimEntityRenderTransform(sim_entity *Entity, real32 Alpha)
{
    return Mat4FromTransform(SimEntityRenderPose(Entity, Alpha));
}
