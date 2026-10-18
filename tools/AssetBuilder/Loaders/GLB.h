#ifndef GLB_H
#define GLB_H

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#include "EngaFormat.h"
#include "Json.h"

#define GLB_FLOAT  5126
#define GLB_UBYTE  5121
#define GLB_USHORT 5123
#define GLB_UINT   5125

#define GLB_MAGIC      0x46546C67
#define GLB_JSON_CHUNK_MARKER 0x4E4F534A
#define GLB_BIN_CHUNK_MARKER  0x004E4942

#define GLB_VERSION 2

#define GLB_HEADER_SIZE           12
#define GLB_HEADER_MAGIC_OFFSET   0
#define GLB_HEADER_VERSION_OFFSET 4
#define GLB_HEADER_LENGTH_OFFSET  8

#define GLB_CHUNK_HEADER_SIZE   8
#define GLB_CHUNK_LENGTH_OFFSET 0
#define GLB_CHUNK_TYPE_OFFSET   4

struct glb_file
{
    json_token *Root;
    json_token *Accessors;
    json_token *BufferViews;
    uint8      *Bin;
    uint32      BinSize;
};

struct glb_geometry
{
    void*             Blob;
    uint64            BlobSize;
    enga_vertex*      Vertices;
    uint32*           Indices;
    enga_skin_vertex* Skin;
    uint32            VertexCount;
    uint32            IndexCount;
};

struct glb_skeleton
{
    char       *Name;
    enga_joint *Joints;
    uint32      JointCount;
    uint32     *NodeToJoint;
    uint32      NodeCount;
};

struct glb_animation
{
    void   *Blob;
    uint64  BlobSize;
    real32  Duration;
    uint32  ChannelCount;
    uint32  FloatCount;
};


internal uint8 *GLBChunk(uint8 *Data, uint32 Size, uint32 *At, uint32 Type, uint32 *OutSize)
{
    Assert(*At + GLB_CHUNK_HEADER_SIZE <= Size);
    uint32 Length    = *(uint32 *)&Data[*At + GLB_CHUNK_LENGTH_OFFSET];
    uint32 ChunkType = *(uint32 *)&Data[*At + GLB_CHUNK_TYPE_OFFSET];
    Assert(ChunkType == Type && Length <= Size - *At - GLB_CHUNK_HEADER_SIZE);

    uint8 *Result = &Data[*At + GLB_CHUNK_HEADER_SIZE];
    *At += GLB_CHUNK_HEADER_SIZE + Length;
    *OutSize = Length;

    return Result;
}

internal glb_file ParseGLB(memory_arena *Arena, void *Data, uint32 Size)
{
    uint8 *Bytes = (uint8 *)Data;

    Assert(Bytes && Size >= GLB_HEADER_SIZE);
    Assert(*(uint32 *)&Bytes[GLB_HEADER_MAGIC_OFFSET] == GLB_MAGIC);
    Assert(*(uint32 *)&Bytes[GLB_HEADER_VERSION_OFFSET] == GLB_VERSION);
    Assert(*(uint32 *)&Bytes[GLB_HEADER_LENGTH_OFFSET] <= Size);

    uint32 At = GLB_HEADER_SIZE;
    uint32 JsonSize = 0;
    uint8 *Json = GLBChunk(Bytes, Size, &At, GLB_JSON_CHUNK_MARKER, &JsonSize);

    uint32 BinSize = 0;
    uint8 *Bin = GLBChunk(Bytes, Size, &At, GLB_BIN_CHUNK_MARKER, &BinSize);

    glb_file Result = {};
    Result.Root = JsonParse(Arena, Json, JsonSize, 0);
    Assert(Result.Root);

    Result.Accessors   = JsonGet(Result.Root, "accessors");
    Result.BufferViews = JsonGet(Result.Root, "bufferViews");
    Result.Bin     = Bin;
    Result.BinSize = BinSize;

    return Result;
}

internal uint32 GLBComponentSize(uint32 ComponentType)
{
    switch (ComponentType)
    {
        case GLB_FLOAT:  return 4;
        case GLB_UINT:   return 4;
        case GLB_USHORT: return 2;
        case GLB_UBYTE:  return 1;
    }

    return 0;
}

internal uint32 GLBReadUInt(uint8 *Element, uint32 ComponentType, uint32 Component)
{
    switch (ComponentType)
    {
        case GLB_UBYTE:  return ((uint8 *)Element)[Component];
        case GLB_USHORT: return ((uint16 *)Element)[Component];
        case GLB_UINT:   return ((uint32 *)Element)[Component];
    }

    return 0;
}

internal real32 GLBReadUnorm(uint8 *Element, uint32 ComponentType, uint32 Component)
{
    switch (ComponentType)
    {
        case GLB_FLOAT:  return ((real32 *)Element)[Component];
        case GLB_UBYTE:  return (real32)((uint8 *)Element)[Component] / 255.0f;
        case GLB_USHORT: return (real32)((uint16 *)Element)[Component] / 65535.0f;
    }

    return 0.0f;
}

internal uint8 *GLBViewData(glb_file *File, uint32 ViewIndex, uint32 *OutSize, uint32 *OutStride)
{
    json_token *View = JsonAt(File->BufferViews, ViewIndex);
    Assert(View);

    if (JsonU32(JsonGet(View, "buffer"), 0) != 0)
    {
        DebugLog("GLB: bufferView %u refers to a second buffer, unsupported\n", ViewIndex);
        return 0;
    }

    uint32 Offset = JsonU32(JsonGet(View, "byteOffset"), 0);
    uint32 Length = JsonU32(JsonGet(View, "byteLength"), 0);
    Assert(Offset <= File->BinSize && Length <= File->BinSize - Offset);

    *OutSize   = Length;
    *OutStride = JsonU32(JsonGet(View, "byteStride"), 0);
    return File->Bin + Offset;
}

internal uint8 *GLBAccessorData(glb_file *File, json_token *AccessorIndex, uint32 ComponentCount, uint32 *OutCount, uint32 *OutComponentType, uint32 *OutStride)
{
    *OutCount         = 0;
    *OutComponentType = 0;
    *OutStride        = 0;

    if (!AccessorIndex)
    {
        return 0;
    }

    json_token *Accessor = JsonAt(File->Accessors, JsonU32(AccessorIndex, 0));
    Assert(Accessor);

    json_token *ViewIndex = JsonGet(Accessor, "bufferView");
    if (!ViewIndex)
    {
        DebugLog("GLB: accessor without a bufferView is unsupported\n");
        return 0;
    }

    uint32 ComponentType = JsonU32(JsonGet(Accessor, "componentType"), 0);
    uint32 ElementSize   = GLBComponentSize(ComponentType) * ComponentCount;
    if (!ElementSize)
    {
        DebugLog("GLB: unsupported componentType %u\n", ComponentType);
        return 0;
    }

    uint32 ViewSize   = 0;
    uint32 ViewStride = 0;
    uint8 *ViewData   = GLBViewData(File, JsonU32(ViewIndex, 0), &ViewSize, &ViewStride);
    if (!ViewData)
    {
        return 0;
    }

    uint32 Stride = ViewStride ? ViewStride : ElementSize;
    Assert(Stride >= ElementSize);

    uint32 Offset = JsonU32(JsonGet(Accessor, "byteOffset"), 0);
    uint32 Count  = JsonU32(JsonGet(Accessor, "count"), 0);
    Assert(Count && Offset <= ViewSize);

    uint64 Span = (uint64)(Count - 1) * Stride + ElementSize;
    Assert(Span <= (uint64)(ViewSize - Offset));

    *OutCount         = Count;
    *OutComponentType = ComponentType;
    *OutStride        = Stride;
    return &ViewData[Offset];
}

internal glb_geometry GLBMeshGeometry(memory_arena *Arena, glb_file *File, json_token *Mesh)
{
    glb_geometry Result = {};

    json_token *Prim       = JsonAt(JsonGet(Mesh, "primitives"), 0);
    json_token *Attributes = JsonGet(Prim, "attributes");

    uint32 PosCount  = 0;
    uint32 PosType   = 0;
    uint32 PosStride = 0;
    uint8 *Pos = GLBAccessorData(File, JsonGet(Attributes, "POSITION"), 3, &PosCount, &PosType, &PosStride);
    Assert(Pos && PosType == GLB_FLOAT && PosCount);

    uint32 NormalCount  = 0;
    uint32 NormalType   = 0;
    uint32 NormalStride = 0;
    uint8 *Normal = GLBAccessorData(File, JsonGet(Attributes, "NORMAL"), 3, &NormalCount, &NormalType, &NormalStride);
    if (Normal && NormalType != GLB_FLOAT)
    {
        DebugLog("GLB: NORMAL is not float, ignored\n");
        Normal = 0;
    }

    uint32 UVCount  = 0;
    uint32 UVType   = 0;
    uint32 UVStride = 0;
    uint8 *UV = GLBAccessorData(File, JsonGet(Attributes, "TEXCOORD_0"), 2, &UVCount, &UVType, &UVStride);
    if (UV && UVType != GLB_FLOAT)
    {
        DebugLog("GLB: TEXCOORD_0 is not float, ignored\n");
        UV = 0;
    }

    uint32 JointsCount  = 0;
    uint32 JointsType   = 0;
    uint32 JointsStride = 0;
    uint8 *Joints = GLBAccessorData(File, JsonGet(Attributes, "JOINTS_0"), 4, &JointsCount, &JointsType, &JointsStride);

    uint32 WeightsCount  = 0;
    uint32 WeightsType   = 0;
    uint32 WeightsStride = 0;
    uint8 *Weights = GLBAccessorData(File, JsonGet(Attributes, "WEIGHTS_0"), 4, &WeightsCount, &WeightsType, &WeightsStride);

    if (Joints && JointsType != GLB_UBYTE && JointsType != GLB_USHORT)
    {
        DebugLog("GLB: JOINTS_0 must be unsigned byte or short, skin ignored\n");
        Joints = 0;
    }

    if (Weights && WeightsType == GLB_UINT)
    {
        DebugLog("GLB: WEIGHTS_0 must be float or normalized byte/short, skin ignored\n");
        Weights = 0;
    }

    bool32 IsSkinned = Joints && Weights && JointsCount == PosCount && WeightsCount == PosCount;

    json_token *IndicesRef = JsonGet(Prim, "indices");

    uint32 SourceIndexCount = 0;
    uint32 IndexType        = 0;
    uint32 IndexStride      = 0;
    uint8 *SourceIndices = GLBAccessorData(File, IndicesRef, 1, &SourceIndexCount, &IndexType, &IndexStride);
    if (IndicesRef && !SourceIndices)
    {
        DebugLog("GLB: primitive declares indices but they could not be read\n");
        return Result;
    }

    Assert(!SourceIndices || IndexType == GLB_USHORT || IndexType == GLB_UINT);

    uint32 VertexCount = PosCount;
    uint32 IndexCount  = SourceIndices ? SourceIndexCount : PosCount;

    uint64 VertexBytes = (uint64)VertexCount * sizeof(enga_vertex);
    uint64 IndexBytes  = (uint64)IndexCount * sizeof(uint32);
    uint64 SkinBytes   = IsSkinned ? (uint64)VertexCount * sizeof(enga_skin_vertex) : 0;

    uint8 *Blob = (uint8 *)PushSize(Arena, VertexBytes + IndexBytes + SkinBytes);

    enga_vertex      *Out     = (enga_vertex *)Blob;
    uint32           *OutIndx = (uint32 *)&Blob[VertexBytes];
    enga_skin_vertex *OutSkin = IsSkinned ? (enga_skin_vertex *)&Blob[VertexBytes + IndexBytes] : 0;

    if (IsSkinned)
    {
        for (uint32 v = 0; v < VertexCount; ++v)
        {
            uint8 *SrcJoints  = &Joints[(memory_size)v * JointsStride];
            uint8 *SrcWeights = &Weights[(memory_size)v * WeightsStride];

            enga_skin_vertex *Dst = &OutSkin[v];

            real32 Total = 0.0f;
            for (uint32 k = 0; k < ENGA_MAX_SKIN_INFLUENCES; ++k)
            {
                Dst->Joints[k]  = (uint16)GLBReadUInt(SrcJoints, JointsType, k);
                Dst->Weights[k] = GLBReadUnorm(SrcWeights, WeightsType, k);
                Total += Dst->Weights[k];
            }

            if (Total > 1e-6f)
            {
                for (uint32 k = 0; k < ENGA_MAX_SKIN_INFLUENCES; ++k)
                {
                    Dst->Weights[k] /= Total;
                }
            }
            else
            {
                Dst->Joints[0]  = 0;
                Dst->Weights[0] = 1.0f;
            }
        }
    }

    for (uint32 v = 0; v < VertexCount; ++v)
    {
        real32 *SrcPos = (real32 *)&Pos[(memory_size)v * PosStride];

        enga_vertex *Dst = &Out[v];
        Dst->Pos[0]    = SrcPos[0];
        Dst->Pos[1]    = SrcPos[1];
        Dst->Pos[2]    = SrcPos[2];
        Dst->Normal[0] = 0.0f;
        Dst->Normal[1] = 1.0f;
        Dst->Normal[2] = 0.0f;
        Dst->Color[0]  = 1.0f;
        Dst->Color[1]  = 1.0f;
        Dst->Color[2]  = 1.0f;
        Dst->UV[0]     = 0.0f;
        Dst->UV[1]     = 0.0f;

        if (Normal && v < NormalCount)
        {
            real32 *SrcNormal = (real32 *)&Normal[(memory_size)v * NormalStride];
            Dst->Normal[0] = SrcNormal[0];
            Dst->Normal[1] = SrcNormal[1];
            Dst->Normal[2] = SrcNormal[2];
        }

        if (UV && v < UVCount)
        {
            real32 *SrcUV = (real32 *)&UV[(memory_size)v * UVStride];
            Dst->UV[0] = SrcUV[0];
            Dst->UV[1] = SrcUV[1];
        }
    }

    for (uint32 i = 0; i < IndexCount; ++i)
    {
        uint32 Src = i;
        if (SourceIndices)
        {
            uint8 *SrcIndex = &SourceIndices[(memory_size)i * IndexStride];
            Src = (IndexType == GLB_USHORT) ? *(uint16 *)SrcIndex : *(uint32 *)SrcIndex;
        }
        Assert(Src < VertexCount);

        OutIndx[i] = Src;
    }

    if (!Normal)
    {
        DebugLog("GLB: mesh has no NORMAL attribute, generating from geometry\n");

        for (uint32 v = 0; v < VertexCount; ++v)
        {
            Out[v].Normal[0] = 0.0f;
            Out[v].Normal[1] = 0.0f;
            Out[v].Normal[2] = 0.0f;
        }

        for (uint32 i = 0; i + 2 < IndexCount; i += 3)
        {
            enga_vertex *A = &Out[OutIndx[i + 0]];
            enga_vertex *B = &Out[OutIndx[i + 1]];
            enga_vertex *C = &Out[OutIndx[i + 2]];

            real32 ABx = B->Pos[0] - A->Pos[0];
            real32 ABy = B->Pos[1] - A->Pos[1];
            real32 ABz = B->Pos[2] - A->Pos[2];

            real32 ACx = C->Pos[0] - A->Pos[0];
            real32 ACy = C->Pos[1] - A->Pos[1];
            real32 ACz = C->Pos[2] - A->Pos[2];

            real32 Nx = ABy * ACz - ABz * ACy;
            real32 Ny = ABz * ACx - ABx * ACz;
            real32 Nz = ABx * ACy - ABy * ACx;

            enga_vertex *Face[3] = { A, B, C };
            for (uint32 f = 0; f < 3; ++f)
            {
                Face[f]->Normal[0] += Nx;
                Face[f]->Normal[1] += Ny;
                Face[f]->Normal[2] += Nz;
            }
        }

        for (uint32 v = 0; v < VertexCount; ++v)
        {
            real32 *N = Out[v].Normal;

            real32 Length = SquareRoot(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
            if (Length > 1e-8f)
            {
                N[0] /= Length;
                N[1] /= Length;
                N[2] /= Length;
            }
            else
            {
                N[0] = 0.0f;
                N[1] = 1.0f;
                N[2] = 0.0f;
            }
        }

        real32 Center[3] = { 0.0f, 0.0f, 0.0f };
        for (uint32 v = 0; v < VertexCount; ++v)
        {
            Center[0] += Out[v].Pos[0];
            Center[1] += Out[v].Pos[1];
            Center[2] += Out[v].Pos[2];
        }
        Center[0] /= (real32)VertexCount;
        Center[1] /= (real32)VertexCount;
        Center[2] /= (real32)VertexCount;

        for (uint32 v = 0; v < VertexCount; ++v)
        {
            real32 *N = Out[v].Normal;

            real32 Outward[3];
            Outward[0] = Out[v].Pos[0] - Center[0];
            Outward[1] = Out[v].Pos[1] - Center[1];
            Outward[2] = Out[v].Pos[2] - Center[2];

            real32 Facing = N[0] * Outward[0] + N[1] * Outward[1] + N[2] * Outward[2];
            if (Facing < -1e-4f)
            {
                N[0] = -N[0];
                N[1] = -N[1];
                N[2] = -N[2];
            }
        }
    }

    Result.Blob        = Blob;
    Result.BlobSize    = VertexBytes + IndexBytes + SkinBytes;
    Result.Vertices    = Out;
    Result.Indices     = OutIndx;
    Result.Skin        = OutSkin;
    Result.VertexCount = VertexCount;
    Result.IndexCount  = IndexCount;

    return Result;
}

internal glb_skeleton GLBSkinSkeleton(memory_arena *Arena, glb_file *File, json_token *Skin)
{
    glb_skeleton Result = {};

    json_token *Nodes      = JsonGet(File->Root, "nodes");
    json_token *JointNodes = JsonGet(Skin, "joints");
    Assert(Nodes && JointNodes && JointNodes->Count);

    uint32 NodeCount  = Nodes->Count;
    uint32 JointCount = JointNodes->Count;

    uint32 *NodeParent  = PushArray(Arena, NodeCount, uint32);
    uint32 *NodeToJoint = PushArray(Arena, NodeCount, uint32);
    for (uint32 n = 0; n < NodeCount; ++n)
    {
        NodeParent[n]  = ENGA_JOINT_NONE;
        NodeToJoint[n] = ENGA_JOINT_NONE;
    }

    for (uint32 n = 0; n < NodeCount; ++n)
    {
        json_token *Children = JsonGet(JsonAt(Nodes, n), "children");
        for (uint32 c = 0; c < (Children ? Children->Count : 0); ++c)
        {
            uint32 Child = JsonU32(JsonAt(Children, c), 0);
            Assert(Child < NodeCount);
            NodeParent[Child] = n;
        }
    }

    uint32 InverseBindCount  = 0;
    uint32 InverseBindType   = 0;
    uint32 InverseBindStride = 0;
    uint8 *InverseBind = GLBAccessorData(File, JsonGet(Skin, "inverseBindMatrices"), 16, &InverseBindCount, &InverseBindType, &InverseBindStride);
    Assert(InverseBind && InverseBindType == GLB_FLOAT && InverseBindCount >= JointCount);

    for (uint32 j = 0; j < JointCount; ++j)
    {
        uint32 NodeIndex = JsonU32(JsonAt(JointNodes, j), 0);
        Assert(NodeIndex < NodeCount);
        NodeToJoint[NodeIndex] = j;
    }

    enga_joint *Joints = PushArray(Arena, JointCount, enga_joint);
    for (uint32 j = 0; j < JointCount; ++j)
    {
        uint32 NodeIndex = JsonU32(JsonAt(JointNodes, j), 0);
        json_token *Node = JsonAt(Nodes, NodeIndex);
        if (JsonGet(Node, "matrix"))
        {
            DebugLog("GLB: joint node %u uses a matrix, only translation/rotation/scale are supported\n", NodeIndex);
            Assert(!"joint node uses a matrix");
        }

        enga_joint *Joint = &Joints[j];
        ZeroStruct(*Joint);

        char *JointName = JsonCString(JsonGet(Node, "name"));
        if (JointName)
        {
            AppendString(Joint->Name, ENGA_MAX_ASSET_NAME, 0, JointName);
        }

        uint32 ParentNode = NodeParent[NodeIndex];
        Joint->Parent = (ParentNode != ENGA_JOINT_NONE) ? NodeToJoint[ParentNode] : ENGA_JOINT_NONE;
        if (Joint->Parent != ENGA_JOINT_NONE && Joint->Parent > j)
        {
            DebugLog("GLB: joint %u is listed before its parent, skin joints must be parent-first\n", j);
            Assert(!"skin joints are not parent-first");
        }

        CopySize(sizeof(Joint->InverseBind), &InverseBind[(memory_size)j * InverseBindStride], Joint->InverseBind);

        Joint->Rotation[3] = 1.0f;
        Joint->Scale[0]    = 1.0f;
        Joint->Scale[1]    = 1.0f;
        Joint->Scale[2]    = 1.0f;
        JsonR32Array(JsonGet(Node, "translation"), Joint->Translation, 3);
        JsonR32Array(JsonGet(Node, "rotation"), Joint->Rotation, 4);
        JsonR32Array(JsonGet(Node, "scale"), Joint->Scale, 3);
    }

    char *Name = JsonCString(JsonGet(Skin, "name"));
    if (!Name)
    {
        Name = JsonCString(JsonGet(JsonAt(Nodes, JsonU32(JsonAt(JointNodes, 0), 0)), "name"));
    }

    Result.Name        = Name;
    Result.Joints      = Joints;
    Result.JointCount  = JointCount;
    Result.NodeToJoint = NodeToJoint;
    Result.NodeCount   = NodeCount;

    return Result;
}

struct glb_channel_source
{
    uint32  Joint;
    uint32  Path;
    uint32  Interpolation;
    uint32  KeyCount;
    uint32  ValueStep;
    uint32  ValueOffset;
    uint8  *Times;
    uint32  TimeStride;
    uint8  *Values;
    uint32  ValueStride;
};

internal bool32 GLBChannelSource(glb_file *File, json_token *Samplers, json_token *Channel, glb_skeleton *Skeleton, glb_channel_source *Out)
{
    json_token *Target = JsonGet(Channel, "target");
    json_token *NodeRef = JsonGet(Target, "node");
    if (!NodeRef)
    {
        return false;
    }

    uint32 Node = JsonU32(NodeRef, 0);
    if (Node >= Skeleton->NodeCount || Skeleton->NodeToJoint[Node] == ENGA_JOINT_NONE)
    {
        return false;
    }

    char *PathName = JsonCString(JsonGet(Target, "path"));
    uint32 Path = 0;
    if (PathName && StringsAreEqual(PathName, "translation"))
    {
        Path = ChannelPath_Translation;
    }
    else if (PathName && StringsAreEqual(PathName, "rotation"))
    {
        Path = ChannelPath_Rotation;
    }
    else if (PathName && StringsAreEqual(PathName, "scale"))
    {
        Path = ChannelPath_Scale;
    }
    else
    {
        DebugLog("GLB: animation path '%s' is unsupported, channel skipped\n", PathName ? PathName : "");
        return false;
    }

    json_token *Sampler = JsonAt(Samplers, JsonU32(JsonGet(Channel, "sampler"), 0));
    if (!Sampler)
    {
        return false;
    }

    char *InterpolationName = JsonCString(JsonGet(Sampler, "interpolation"));
    uint32 Interpolation = ChannelInterpolation_Linear;
    uint32 ValueStep     = 1;
    uint32 ValueOffset   = 0;
    if (InterpolationName && StringsAreEqual(InterpolationName, "STEP"))
    {
        Interpolation = ChannelInterpolation_Step;
    }
    else if (InterpolationName && StringsAreEqual(InterpolationName, "CUBICSPLINE"))
    {
        ValueStep   = 3;
        ValueOffset = 1;
    }

    uint32 ComponentCount = EngaChannelComponentCount((enga_channel_path)Path);

    uint32 TimeCount  = 0;
    uint32 TimeType   = 0;
    uint32 TimeStride = 0;
    uint8 *Times = GLBAccessorData(File, JsonGet(Sampler, "input"), 1, &TimeCount, &TimeType, &TimeStride);

    uint32 ValueCount  = 0;
    uint32 ValueType   = 0;
    uint32 ValueStride = 0;
    uint8 *Values = GLBAccessorData(File, JsonGet(Sampler, "output"), ComponentCount, &ValueCount, &ValueType, &ValueStride);

    if (!Times || !Values || TimeType != GLB_FLOAT || ValueType != GLB_FLOAT || ValueCount < TimeCount * ValueStep)
    {
        DebugLog("GLB: animation sampler must use float input/output, channel skipped\n");
        return false;
    }

    Out->Joint         = Skeleton->NodeToJoint[Node];
    Out->Path          = Path;
    Out->Interpolation = Interpolation;
    Out->KeyCount      = TimeCount;
    Out->ValueStep     = ValueStep;
    Out->ValueOffset   = ValueOffset;
    Out->Times         = Times;
    Out->TimeStride    = TimeStride;
    Out->Values        = Values;
    Out->ValueStride   = ValueStride;

    return true;
}

internal glb_animation GLBSkeletonAnimation(memory_arena *Arena, glb_file *File, json_token *Animation, glb_skeleton *Skeleton)
{
    glb_animation Result = {};

    json_token *Channels = JsonGet(Animation, "channels");
    json_token *Samplers = JsonGet(Animation, "samplers");
    if (!Channels || !Samplers)
    {
        return Result;
    }

    uint32 ChannelCount = 0;
    uint32 FloatCount   = 0;
    for (uint32 c = 0; c < Channels->Count; ++c)
    {
        glb_channel_source Source;
        if (GLBChannelSource(File, Samplers, JsonAt(Channels, c), Skeleton, &Source))
        {
            ChannelCount++;
            FloatCount += Source.KeyCount * (1 + EngaChannelComponentCount((enga_channel_path)Source.Path));
        }
    }

    if (!ChannelCount)
    {
        return Result;
    }

    uint64 ChannelBytes = (uint64)ChannelCount * sizeof(enga_animation_channel);
    uint64 FloatBytes   = (uint64)FloatCount * sizeof(real32);

    uint8 *Blob = (uint8 *)PushSize(Arena, ChannelBytes + FloatBytes);
    enga_animation_channel *OutChannels = (enga_animation_channel *)Blob;
    real32                 *OutFloats   = (real32 *)&Blob[ChannelBytes];

    real32 Duration = 0.0f;
    uint32 Channel  = 0;
    uint32 Float    = 0;
    for (uint32 c = 0; c < Channels->Count; ++c)
    {
        glb_channel_source Source;
        if (!GLBChannelSource(File, Samplers, JsonAt(Channels, c), Skeleton, &Source))
        {
            continue;
        }

        uint32 ComponentCount = EngaChannelComponentCount((enga_channel_path)Source.Path);

        enga_animation_channel *Dst = &OutChannels[Channel++];
        Dst->Joint         = Source.Joint;
        Dst->Path          = Source.Path;
        Dst->Interpolation = Source.Interpolation;
        Dst->KeyCount      = Source.KeyCount;
        Dst->FirstTime     = Float;
        Dst->FirstValue    = Float + Source.KeyCount;

        for (uint32 k = 0; k < Source.KeyCount; ++k)
        {
            real32 Time = *(real32 *)&Source.Times[(memory_size)k * Source.TimeStride];
            OutFloats[Dst->FirstTime + k] = Time;
            Duration = Maximum(Duration, Time);

            uint32 ValueIndex = k * Source.ValueStep + Source.ValueOffset;
            real32 *Value = (real32 *)&Source.Values[(memory_size)ValueIndex * Source.ValueStride];
            for (uint32 e = 0; e < ComponentCount; ++e)
            {
                OutFloats[Dst->FirstValue + k * ComponentCount + e] = Value[e];
            }
        }

        Float += Source.KeyCount * (1 + ComponentCount);
    }

    Assert(Channel == ChannelCount && Float == FloatCount);

    Result.Blob         = Blob;
    Result.BlobSize     = ChannelBytes + FloatBytes;
    Result.Duration     = Duration;
    Result.ChannelCount = ChannelCount;
    Result.FloatCount   = FloatCount;

    return Result;
}

#endif
