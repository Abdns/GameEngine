#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#include "Collider.h"
#include "SimEntity.cpp"
#include "Shapes.cpp"
#include "Broadphase.cpp"
#include "SimRegion.cpp"

#define PHYSICS_TICK                      (1.0f / 60.0f)
#define PHYSICS_ACCUMULATOR_CAP           (4.0f * PHYSICS_TICK)
#define PHYSICS_SUBSTEPS                  4
#define PHYSICS_RESTITUTION               0.5f
#define PHYSICS_RESTITUTION_MIN_SPEED     1.0f
#define PHYSICS_SOLVER_ITERATIONS         4
#define PHYSICS_MAX_BIAS_SPEED            1.5f
#define PHYSICS_MAX_CONTACTS              512
#define PHYSICS_MAX_PAIR_CONTACTS         8
#define PHYSICS_CONTACT_MERGE_DISTANCE_SQ 0.0001f
#define PHYSICS_BIAS_FACTOR               0.2f
#define PHYSICS_PENETRATION_SLOP          0.01f
#define PHYSICS_FRICTION                  0.4f
#define PHYSICS_ANGULAR_DAMPING           0.5f
#define PHYSICS_GRAVITY_Y                 -9.8f
#define PHYSICS_PAIRS_PER_ENTITY          16
#define SAT_MAX_POLYGON                   32
#define SAT_REFERENCE_BIAS                0.001f

struct contact
{
    sim_entity *A;
    sim_entity *B;
    Vector3     Point;
    Vector3     Normal;
    real32      Depth;
    real32      RestitutionBias;
    real32      AccumulatedNormal;
    Vector3     AccumulatedFriction;
};

struct physics_state
{
    collision_shape *Shapes;
    bool32          *ShapeBuilt;
    uint32           ShapeCapacity;

    contact *Contacts;
    uint32   ContactCapacity;

    broadphase Broad;

    real32 Accumulator;
};

internal void PhysicsInit(physics_state *Physics, memory_arena *Arena, uint32 EntityCapacity, uint32 ShapeCapacity)
{
    ZeroStruct(*Physics);

    Physics->ShapeCapacity = ShapeCapacity;
    Physics->Shapes        = PushArray(Arena, ShapeCapacity, collision_shape);
    Physics->ShapeBuilt    = PushArray(Arena, ShapeCapacity, bool32);
    ZeroArray(ShapeCapacity, Physics->ShapeBuilt);

    Physics->ContactCapacity = PHYSICS_MAX_CONTACTS;
    Physics->Contacts        = PushArray(Arena, Physics->ContactCapacity, contact);

    BroadphaseInit(&Physics->Broad, Arena, EntityCapacity, EntityCapacity * PHYSICS_PAIRS_PER_ENTITY);

    Physics->Accumulator = 0.0f;
}

internal void PhysicsSetMeshShape(physics_state *Physics, memory_arena *Arena, uint32 MeshHandle, collision Mesh)
{
    Assert(MeshHandle < Physics->ShapeCapacity);

    Physics->Shapes[MeshHandle]     = BuildCollisionShape(Arena, Mesh);
    Physics->ShapeBuilt[MeshHandle] = true;
}

internal collision_shape *PhysicsShapeFor(physics_state *Physics, sim_entity *Entity)
{
    Assert(Entity->MeshHandle < Physics->ShapeCapacity);
    Assert(Physics->ShapeBuilt[Entity->MeshHandle]);

    return Physics->Shapes + Entity->MeshHandle;
}

internal void PhysicsSetMass(physics_state *Physics, sim_entity *Entity, bool32 Static)
{
    if (Static)
    {
        Entity->InvMass    = 0.0f;
        Entity->InvInertia = 0.0f;

        return;
    }

    real32 InertiaFactor = PhysicsShapeFor(Physics, Entity)->InertiaFactor;

    Entity->InvMass    = 1.0f;
    Entity->InvInertia = (InertiaFactor > Epsilon32) ? (1.0f / InertiaFactor) : 0.0f;
}

internal void PhysicsComputeBounds(physics_state *Physics, sim_entity *Entity)
{
    collision_shape *Shape = PhysicsShapeFor(Physics, Entity);

    if (Shape->Type == Shape_Sphere)
    {
        Vector3 Center = Entity->Current.Position + QuatRotate(Entity->Current.Orientation, Shape->Center);

        Entity->Bounds = Rect3CenterRadius(Center, Shape->Radius);

        return;
    }

    ComputeWorldAABB(Entity->Current.Position, Entity->Current.Orientation, Shape->BoundsMin, Shape->BoundsMax, &Entity->Bounds.Min, &Entity->Bounds.Max);
}

inline real32 PhysicsInvMass(sim_entity *Entity)
{
    return Entity->Updatable ? Entity->InvMass : 0.0f;
}

inline real32 PhysicsInvInertia(sim_entity *Entity)
{
    return Entity->Updatable ? Entity->InvInertia : 0.0f;
}

internal Vector3 PhysicsRelativeVelocity(contact *Contact)
{
    Vector3 rA = Contact->Point - Contact->A->Current.Position;
    Vector3 rB = Contact->Point - Contact->B->Current.Position;

    return (Contact->A->dPosition + Cross(Contact->A->dOrientation, rA))
         - (Contact->B->dPosition + Cross(Contact->B->dOrientation, rB));
}

internal bool32 PhysicsPushContact(contact *Contacts, uint32 *Count, uint32 MaxContacts, sim_entity *A, sim_entity *B, Vector3 Point, Vector3 Normal, real32 Depth)
{
    if (*Count >= MaxContacts)
    {
        return false;
    }

    for (uint32 Existing = 0; Existing < *Count; ++Existing)
    {
        if (LengthSq(Contacts[Existing].Point - Point) < PHYSICS_CONTACT_MERGE_DISTANCE_SQ)
        {
            return false;
        }
    }

    contact *Contact = Contacts + (*Count)++;

    Contact->A                   = A;
    Contact->B                   = B;
    Contact->Point               = Point;
    Contact->Normal              = Normal;
    Contact->Depth               = Depth;
    Contact->RestitutionBias     = 0.0f;
    Contact->AccumulatedNormal   = 0.0f;
    Contact->AccumulatedFriction = Vector3(0.0f, 0.0f, 0.0f);

    return true;
}

internal uint32 CollectSphereSphereContacts(collision_shape *ShapeA, collision_shape *ShapeB, sim_entity *A, sim_entity *B, contact *Contacts, uint32 MaxContacts)
{
    Vector3 CenterA = A->Current.Position + QuatRotate(A->Current.Orientation, ShapeA->Center);
    Vector3 CenterB = B->Current.Position + QuatRotate(B->Current.Orientation, ShapeB->Center);

    Vector3 Delta      = CenterA - CenterB;
    real32  RadiusSum  = ShapeA->Radius + ShapeB->Radius;
    real32  DistanceSq = LengthSq(Delta);

    if (DistanceSq >= RadiusSum * RadiusSum)
    {
        return 0;
    }

    real32  Distance = SquareRoot(DistanceSq);
    Vector3 Normal   = (Distance > Epsilon32) ? (1.0f / Distance) * Delta : Vector3(0.0f, 1.0f, 0.0f);
    real32  Depth    = RadiusSum - Distance;
    Vector3 Point    = CenterB + Normal * (ShapeB->Radius - 0.5f * Depth);

    uint32 Count = 0;
    PhysicsPushContact(Contacts, &Count, MaxContacts, A, B, Point, Normal, Depth);

    return Count;
}

internal uint32 CollectSphereConvexContacts(collision_shape *SphereShape, collision_shape *ConvexShape, sim_entity *Sphere, sim_entity *Convex, contact *Contacts, uint32 MaxContacts)
{
    Vector3 Center      = Sphere->Current.Position + QuatRotate(Sphere->Current.Orientation, SphereShape->Center);
    Vector3 LocalCenter = QuatInverseRotate(Convex->Current.Orientation, Center - Convex->Current.Position);

    uint32 PlaneIndex;
    real32 SignedDistance;
    if (!ShapeDeepestPlane(ConvexShape, LocalCenter, &PlaneIndex, &SignedDistance))
    {
        return 0;
    }

    if (SignedDistance > SphereShape->Radius)
    {
        return 0;
    }

    Vector3 Normal = QuatRotate(Convex->Current.Orientation, ConvexShape->Planes[PlaneIndex].Normal);
    real32  Depth  = SphereShape->Radius - SignedDistance;
    Vector3 Point  = Center - Normal * (SphereShape->Radius - 0.5f * Depth);

    uint32 Count = 0;
    PhysicsPushContact(Contacts, &Count, MaxContacts, Sphere, Convex, Point, Normal, Depth);

    return Count;
}

internal Vector3 ShapeSupportWorld(collision_shape *Shape, sim_entity *Entity, Vector3 WorldDirection)
{
    Vector3 LocalDirection = QuatInverseRotate(Entity->Current.Orientation, WorldDirection);

    real32  BestDot    = -REAL32_LARGE;
    Vector3 BestVertex = Shape->Vertices[0];

    for (uint32 Index = 0; Index < Shape->VertexCount; ++Index)
    {
        real32 Projection = Dot(LocalDirection, Shape->Vertices[Index]);

        if (Projection > BestDot)
        {
            BestDot    = Projection;
            BestVertex = Shape->Vertices[Index];
        }
    }

    return Entity->Current.Position + QuatRotate(Entity->Current.Orientation, BestVertex);
}

internal real32 SATQueryFaces(collision_shape *ShapeA, sim_entity *A, collision_shape *ShapeB, sim_entity *B, uint32 *OutFace)
{
    real32 BestSeparation = -REAL32_LARGE;
    uint32 BestFace       = 0;

    for (uint32 Index = 0; Index < ShapeA->PlaneCount; ++Index)
    {
        if (!ShapeA->Faces[Index].VertexCount)
        {
            continue;
        }

        shape_plane *Plane = ShapeA->Planes + Index;

        Vector3 WorldNormal = QuatRotate(A->Current.Orientation, Plane->Normal);
        Vector3 WorldPoint  = A->Current.Position + QuatRotate(A->Current.Orientation, Plane->Normal * Plane->Distance);

        Vector3 Support = ShapeSupportWorld(ShapeB, B, -1.0f * WorldNormal);

        real32 Separation = Dot(WorldNormal, Support - WorldPoint);

        if (Separation > BestSeparation)
        {
            BestSeparation = Separation;
            BestFace       = Index;
        }
    }

    *OutFace = BestFace;

    return BestSeparation;
}

internal uint32 SATFaceWorldPolygon(collision_shape *Shape, sim_entity *Entity, uint32 FaceIndex, Vector3 *Out, uint32 MaxOut)
{
    shape_face *Face  = Shape->Faces + FaceIndex;
    uint32      Count = Minimum(Face->VertexCount, MaxOut);

    for (uint32 Index = 0; Index < Count; ++Index)
    {
        Vector3 Local = Shape->Vertices[Shape->FaceVertices[Face->FirstVertex + Index]];

        Out[Index] = Entity->Current.Position + QuatRotate(Entity->Current.Orientation, Local);
    }

    return Count;
}

internal uint32 SATClipAgainstPlane(Vector3 *In, uint32 InCount, Vector3 PlanePoint, Vector3 PlaneNormal, Vector3 *Out, uint32 MaxOut)
{
    uint32 Count = 0;

    for (uint32 Index = 0; Index < InCount && Count + 2 <= MaxOut; ++Index)
    {
        Vector3 Current = In[Index];
        Vector3 Next    = In[(Index + 1) % InCount];

        real32 DistanceCurrent = Dot(PlaneNormal, Current - PlanePoint);
        real32 DistanceNext    = Dot(PlaneNormal, Next - PlanePoint);

        if (DistanceCurrent <= 0.0f)
        {
            Out[Count++] = Current;
        }

        if (DistanceCurrent * DistanceNext < 0.0f)
        {
            real32 T = DistanceCurrent / (DistanceCurrent - DistanceNext);

            Out[Count++] = Current + T * (Next - Current);
        }
    }

    return Count;
}

internal uint32 CollectConvexContacts(collision_shape *ShapeA, collision_shape *ShapeB, sim_entity *A, sim_entity *B, contact *Contacts, uint32 MaxContacts)
{
    uint32 FaceA, FaceB;

    real32 SeparationA = SATQueryFaces(ShapeA, A, ShapeB, B, &FaceA);
    if (SeparationA > 0.0f)
    {
        return 0;
    }

    real32 SeparationB = SATQueryFaces(ShapeB, B, ShapeA, A, &FaceB);
    if (SeparationB > 0.0f)
    {
        return 0;
    }

    collision_shape *ReferenceShape = ShapeA;
    collision_shape *IncidentShape  = ShapeB;
    sim_entity      *ReferenceEntity = A;
    sim_entity      *IncidentEntity  = B;
    uint32           ReferenceFace   = FaceA;

    if (SeparationB > SeparationA + SAT_REFERENCE_BIAS)
    {
        ReferenceShape  = ShapeB;
        IncidentShape   = ShapeA;
        ReferenceEntity = B;
        IncidentEntity  = A;
        ReferenceFace   = FaceB;
    }

    shape_plane *ReferencePlane = ReferenceShape->Planes + ReferenceFace;

    Vector3 ReferenceNormal = QuatRotate(ReferenceEntity->Current.Orientation, ReferencePlane->Normal);
    Vector3 ReferencePoint  = ReferenceEntity->Current.Position + QuatRotate(ReferenceEntity->Current.Orientation, ReferencePlane->Normal * ReferencePlane->Distance);

    uint32 IncidentFace = 0;
    real32 BestAlignment = REAL32_LARGE;

    for (uint32 Index = 0; Index < IncidentShape->PlaneCount; ++Index)
    {
        if (!IncidentShape->Faces[Index].VertexCount)
        {
            continue;
        }

        real32 Alignment = Dot(QuatRotate(IncidentEntity->Current.Orientation, IncidentShape->Planes[Index].Normal), ReferenceNormal);

        if (Alignment < BestAlignment)
        {
            BestAlignment = Alignment;
            IncidentFace  = Index;
        }
    }

    Vector3 PolygonA[SAT_MAX_POLYGON];
    Vector3 PolygonB[SAT_MAX_POLYGON];
    Vector3 ReferencePolygon[SAT_MAX_POLYGON];

    uint32 ReferenceCount = SATFaceWorldPolygon(ReferenceShape, ReferenceEntity, ReferenceFace, ReferencePolygon, SAT_MAX_POLYGON);
    uint32 ClippedCount   = SATFaceWorldPolygon(IncidentShape, IncidentEntity, IncidentFace, PolygonA, SAT_MAX_POLYGON);

    if (ReferenceCount < 3 || ClippedCount < 3)
    {
        return 0;
    }

    Vector3 *Current = PolygonA;
    Vector3 *Target  = PolygonB;

    for (uint32 Edge = 0; Edge < ReferenceCount && ClippedCount; ++Edge)
    {
        Vector3 Start = ReferencePolygon[Edge];
        Vector3 End   = ReferencePolygon[(Edge + 1) % ReferenceCount];

        Vector3 SideNormal = Cross(End - Start, ReferenceNormal);
        real32  SideLength = Length(SideNormal);
        if (SideLength < Epsilon32)
        {
            continue;
        }
        SideNormal = (1.0f / SideLength) * SideNormal;

        ClippedCount = SATClipAgainstPlane(Current, ClippedCount, Start, SideNormal, Target, SAT_MAX_POLYGON);

        Vector3 *Swap = Current;
        Current = Target;
        Target  = Swap;
    }

    uint32 Count = 0;

    for (uint32 Index = 0; Index < ClippedCount && Count < MaxContacts; ++Index)
    {
        real32 Depth = -Dot(ReferenceNormal, Current[Index] - ReferencePoint);

        if (Depth <= 0.0f)
        {
            continue;
        }

        PhysicsPushContact(Contacts, &Count, MaxContacts, IncidentEntity, ReferenceEntity, Current[Index], ReferenceNormal, Depth);
    }

    return Count;
}

internal uint32 CollectPairContacts(physics_state *Physics, sim_entity *A, sim_entity *B, contact *Contacts, uint32 MaxContacts)
{
    collision_shape *ShapeA = PhysicsShapeFor(Physics, A);
    collision_shape *ShapeB = PhysicsShapeFor(Physics, B);

    if (ShapeA->Type == Shape_Sphere && ShapeB->Type == Shape_Sphere)
    {
        return CollectSphereSphereContacts(ShapeA, ShapeB, A, B, Contacts, MaxContacts);
    }

    if (ShapeA->Type == Shape_Sphere)
    {
        return CollectSphereConvexContacts(ShapeA, ShapeB, A, B, Contacts, MaxContacts);
    }

    if (ShapeB->Type == Shape_Sphere)
    {
        return CollectSphereConvexContacts(ShapeB, ShapeA, B, A, Contacts, MaxContacts);
    }

    return CollectConvexContacts(ShapeA, ShapeB, A, B, Contacts, Minimum(MaxContacts, PHYSICS_MAX_PAIR_CONTACTS));
}

internal void ApplyContactImpulse(contact *Contact, real32 InvDt)
{
    sim_entity *A = Contact->A;
    sim_entity *B = Contact->B;

    real32 InvMassA    = PhysicsInvMass(A);
    real32 InvMassB    = PhysicsInvMass(B);
    real32 InvInertiaA = PhysicsInvInertia(A);
    real32 InvInertiaB = PhysicsInvInertia(B);

    Vector3 rA = Contact->Point - A->Current.Position;
    Vector3 rB = Contact->Point - B->Current.Position;

    Vector3 RelativeVelocity = PhysicsRelativeVelocity(Contact);

    real32 NormalDenom = InvMassA + InvMassB
                       + InvInertiaA * LengthSq(Cross(rA, Contact->Normal))
                       + InvInertiaB * LengthSq(Cross(rB, Contact->Normal));
    if (NormalDenom <= 0.0f)
    {
        return;
    }

    real32 Bias = PHYSICS_BIAS_FACTOR * InvDt * Maximum(Contact->Depth - PHYSICS_PENETRATION_SLOP, 0.0f);
    Bias = Minimum(Bias, PHYSICS_MAX_BIAS_SPEED);
    Bias = Maximum(Bias, Contact->RestitutionBias);

    real32 NormalDelta = (Bias - Dot(RelativeVelocity, Contact->Normal)) / NormalDenom;
    real32 OldNormal   = Contact->AccumulatedNormal;
    Contact->AccumulatedNormal = Maximum(OldNormal + NormalDelta, 0.0f);
    NormalDelta = Contact->AccumulatedNormal - OldNormal;

    Vector3 Impulse = Contact->Normal * NormalDelta;
    A->dPosition           += Impulse * InvMassA;
    A->dOrientation += InvInertiaA * Cross(rA, Impulse);
    B->dPosition           -= Impulse * InvMassB;
    B->dOrientation -= InvInertiaB * Cross(rB, Impulse);

    RelativeVelocity = PhysicsRelativeVelocity(Contact);

    Vector3 Tangent    = RelativeVelocity - Contact->Normal * Dot(RelativeVelocity, Contact->Normal);
    real32  TangentLen = Length(Tangent);
    if (TangentLen < Epsilon32)
    {
        return;
    }
    Tangent = (1.0f / TangentLen) * Tangent;

    real32 TangentDenom = InvMassA + InvMassB
                        + InvInertiaA * LengthSq(Cross(rA, Tangent))
                        + InvInertiaB * LengthSq(Cross(rB, Tangent));
    if (TangentDenom <= 0.0f)
    {
        return;
    }

    real32 TangentDelta = -Dot(RelativeVelocity, Tangent) / TangentDenom;

    Vector3 OldFriction = Contact->AccumulatedFriction;
    Vector3 NewFriction = OldFriction + Tangent * TangentDelta;
    real32  MaxFriction = PHYSICS_FRICTION * Contact->AccumulatedNormal;
    real32  FrictionLen = Length(NewFriction);
    if (FrictionLen > MaxFriction)
    {
        NewFriction = (MaxFriction > 0.0f) ? NewFriction * (MaxFriction / FrictionLen) : Vector3(0.0f, 0.0f, 0.0f);
    }
    Contact->AccumulatedFriction = NewFriction;

    Impulse = NewFriction - OldFriction;
    A->dPosition           += Impulse * InvMassA;
    A->dOrientation += InvInertiaA * Cross(rA, Impulse);
    B->dPosition           -= Impulse * InvMassB;
    B->dOrientation -= InvInertiaB * Cross(rB, Impulse);
}

internal void PhysicsSubStep(physics_state *Physics, sim_region *Region, real32 dt)
{
    for (uint32 Index = 0; Index < Region->EntityCount; ++Index)
    {
        sim_entity *Entity = Region->Entities + Index;

        if (!(Entity->Flags & EntityFlag_Simulates))
        {
            continue;
        }

        if (Entity->Updatable && Entity->InvMass > 0.0f)
        {
            Entity->dPosition.Y += PHYSICS_GRAVITY_Y * dt;
            Entity->Current.Position    += Entity->dPosition * dt;

            Entity->dOrientation = Entity->dOrientation * (1.0f / (1.0f + PHYSICS_ANGULAR_DAMPING * dt));
            Entity->Current.Orientation  = QuatIntegrate(Entity->Current.Orientation, Entity->dOrientation, dt);
        }

        PhysicsComputeBounds(Physics, Entity);
    }

    uint32 PairCount = BroadphaseFindPairs(&Physics->Broad, Region->Entities);

    uint32 ContactCount = 0;
    for (uint32 PairIndex = 0; PairIndex < PairCount && ContactCount < Physics->ContactCapacity; ++PairIndex)
    {
        body_pair *Pair = Physics->Broad.Pairs + PairIndex;

        ContactCount += CollectPairContacts(Physics, Region->Entities + Pair->A, Region->Entities + Pair->B,
                                            Physics->Contacts + ContactCount, Physics->ContactCapacity - ContactCount);
    }

    for (uint32 Index = 0; Index < ContactCount; ++Index)
    {
        contact *Contact = Physics->Contacts + Index;

        real32 ApproachSpeed = -Dot(PhysicsRelativeVelocity(Contact), Contact->Normal);
        Contact->RestitutionBias = (ApproachSpeed > PHYSICS_RESTITUTION_MIN_SPEED) ? PHYSICS_RESTITUTION * ApproachSpeed : 0.0f;
    }

    real32 InvDt = 1.0f / dt;
    for (uint32 Iteration = 0; Iteration < PHYSICS_SOLVER_ITERATIONS; ++Iteration)
    {
        for (uint32 Index = 0; Index < ContactCount; ++Index)
        {
            ApplyContactImpulse(Physics->Contacts + Index, InvDt);
        }
    }
}

internal void PhysicsAccumulate(physics_state *Physics, real32 dt)
{
    Physics->Accumulator = Minimum(Physics->Accumulator + dt, PHYSICS_ACCUMULATOR_CAP);
}

internal bool32 PhysicsNextTick(physics_state *Physics)
{
    if (Physics->Accumulator < PHYSICS_TICK)
    {
        return false;
    }

    Physics->Accumulator -= PHYSICS_TICK;

    return true;
}

internal real32 PhysicsRenderAlpha(physics_state *Physics)
{
    return Physics->Accumulator / PHYSICS_TICK;
}

internal void SimSavePreviousTransforms(sim_region *Region)
{
    for (uint32 Index = 0; Index < Region->EntityCount; ++Index)
    {
        sim_entity *Entity = Region->Entities + Index;

        Entity->Previous = Entity->Current;
    }
}

internal void PhysicsStep(physics_state *Physics, sim_region *Region)
{
    BroadphaseBuildOrder(&Physics->Broad, Region->Entities, Region->EntityCount);

    real32 SubDt = PHYSICS_TICK / (real32)PHYSICS_SUBSTEPS;

    for (uint32 Substep = 0; Substep < PHYSICS_SUBSTEPS; ++Substep)
    {
        PhysicsSubStep(Physics, Region, SubDt);
    }
}
