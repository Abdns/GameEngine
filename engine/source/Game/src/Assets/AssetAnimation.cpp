#pragma once

// Included by AssetStore.cpp after its storage definitions.

// Sample a pose, build joint matrices, then skin mesh vertices on the CPU.

internal void AssetSampleAnimation(asset_store *Store, uint32 AnimationHandle, real32 Time, transform *Pose)
{
    loaded_animation *Animation = &Store->Animations[AnimationHandle];
    loaded_skeleton *Skeleton = &Store->Skeletons[Animation->SkeletonHandle];

    for (uint32 JointIndex = 0; JointIndex < Skeleton->JointCount; ++JointIndex)
    {
        Pose[JointIndex] = Skeleton->Joints[JointIndex].BindPose;
    }

    for (uint32 ChannelIndex = 0; ChannelIndex < Animation->ChannelCount; ++ChannelIndex)
    {
        enga_animation_channel *Channel = &Animation->Channels[ChannelIndex];

        real32 *Times  = &Animation->Floats[Channel->FirstTime];
        real32 *Values = &Animation->Floats[Channel->FirstValue];
        uint32  Width  = EngaChannelComponentCount((enga_channel_path)Channel->Path);

        uint32 Next = 0;
        while (Next < Channel->KeyCount && Times[Next] <= Time)
        {
            ++Next;
        }

        uint32 A = (Next > 0) ? Next - 1 : 0;
        uint32 B = (Next < Channel->KeyCount) ? Next : Channel->KeyCount - 1;

        real32 T = 0.0f;
        if (A != B && Channel->Interpolation == ChannelInterpolation_Linear)
        {
            T = (Time - Times[A]) / (Times[B] - Times[A]);
        }

        real32 *VA = &Values[A * Width];
        real32 *VB = &Values[B * Width];

        transform *Joint = &Pose[Channel->Joint];
        switch ((enga_channel_path)Channel->Path)
        {
            case ChannelPath_Translation:
            {
                Joint->Position = Vector3(Lerp(VA[0], T, VB[0]), Lerp(VA[1], T, VB[1]), Lerp(VA[2], T, VB[2]));
            } break;

            case ChannelPath_Rotation:
            {
                Joint->Orientation = QuatNLerp(Quaternion(VA[0], VA[1], VA[2], VA[3]), Quaternion(VB[0], VB[1], VB[2], VB[3]), T);
            } break;

            case ChannelPath_Scale:
            {
                Joint->Scale = Vector3(Lerp(VA[0], T, VB[0]), Lerp(VA[1], T, VB[1]), Lerp(VA[2], T, VB[2]));
            } break;
        }
    }
}

internal void AssetSkinMatrices(asset_store *Store, uint32 SkeletonHandle, transform *Pose, Matrix4 *World, Matrix4 *Skin)
{
    loaded_skeleton *Skeleton = &Store->Skeletons[SkeletonHandle];

    for (uint32 JointIndex = 0; JointIndex < Skeleton->JointCount; ++JointIndex)
    {
        loaded_joint *Joint = &Skeleton->Joints[JointIndex];
        Matrix4 Local  = Mat4FromTransform(Pose[JointIndex]);
        uint32  Parent = Joint->Parent;

        World[JointIndex] = (Parent == ENGA_JOINT_NONE) ? Local : Mat4Multiply(World[Parent], Local);
        Skin[JointIndex]  = Mat4Multiply(World[JointIndex], Joint->InverseBind);
    }
}

internal enga_vertex *AssetPoseMesh(asset_store *Store, memory_arena *Arena, uint32 MeshHandle, uint32 AnimationHandle, real32 Time)
{
    loaded_mesh *Mesh = &Store->Meshes[MeshHandle];
    enga_skin_vertex *SkinVertices = Mesh->SkinVertices;
    Assert(SkinVertices);

    uint32 Skeleton   = Store->Animations[AnimationHandle].SkeletonHandle;
    uint32 JointCount = Store->Skeletons[Skeleton].JointCount;

    transform *Pose  = PushArray(Arena, JointCount, transform);
    Matrix4   *World = PushArray(Arena, JointCount, Matrix4);
    Matrix4   *Skin  = PushArray(Arena, JointCount, Matrix4);

    AssetSampleAnimation(Store, AnimationHandle, Time, Pose);
    AssetSkinMatrices(Store, Skeleton, Pose, World, Skin);

    uint32       VertexCount = Mesh->VertexCount;
    enga_vertex *Source      = Mesh->Vertices;
    enga_vertex *Result      = PushArray(Arena, VertexCount, enga_vertex);

    for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        enga_vertex      *Src     = &Source[VertexIndex];
        enga_skin_vertex *Weights = &SkinVertices[VertexIndex];

        Vector3 P = Vector3(Src->Pos[0], Src->Pos[1], Src->Pos[2]);
        Vector3 N = Vector3(Src->Normal[0], Src->Normal[1], Src->Normal[2]);

        Vector3 PosedP = Vector3(0.0f, 0.0f, 0.0f);
        Vector3 PosedN = Vector3(0.0f, 0.0f, 0.0f);
        for (uint32 Influence = 0; Influence < ENGA_MAX_SKIN_INFLUENCES; ++Influence)
        {
            real32 Weight = Weights->Weights[Influence];
            uint32 Joint  = Weights->Joints[Influence];
            if (Weight <= 0.0f || Joint >= JointCount)
            {
                continue;
            }

            PosedP += Weight * Mat4Transform(Skin[Joint], P, 1.0f);
            PosedN += Weight * Mat4Transform(Skin[Joint], N, 0.0f);
        }
        PosedN = Normalize(PosedN);

        enga_vertex *Dst = &Result[VertexIndex];
        *Dst = *Src;
        Dst->Pos[0]    = PosedP.X;
        Dst->Pos[1]    = PosedP.Y;
        Dst->Pos[2]    = PosedP.Z;
        Dst->Normal[0] = PosedN.X;
        Dst->Normal[1] = PosedN.Y;
        Dst->Normal[2] = PosedN.Z;
    }

    return Result;
}
