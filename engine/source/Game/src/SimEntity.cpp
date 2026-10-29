#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderDescription.h"
#define ENTITY_STORAGE_NONE 0

enum entity_type
{
    Entity_Null = 0,
    Entity_Floor,
    Entity_Prop,
};

enum entity_flag
{
    EntityFlag_Visible   = 0x1,
};

struct sim_entity
{
    uint32      LowStorageIndex;

    entity_type Type;
    uint32      Flags;

    transform   Pose;

    uint32      MeshHandle;
    material_params Params;

    uint32      AnimationHandle;
    real32      AnimationTime;

};

union sim_entity_reference
{
    sim_entity *Ptr;
    uint32      Index;
};

internal Matrix4 SimEntityRenderTransform(sim_entity *Entity)
{
    return Mat4FromTransform(Entity->Pose);
}
