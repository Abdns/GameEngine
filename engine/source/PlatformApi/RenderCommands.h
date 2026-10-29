#ifndef RENDERCOMMANDS_H
#define RENDERCOMMANDS_H

#include "Types.h"
#include "Memory.h"
#include "EngineMath.h"
#include "RenderDescription.h"

enum command_type
{
    Bind_Camera = 0,
    Bind_Light,
    Load_Mesh,
    Load_Texture,
    Load_Cubemap,
    Load_Material,
    Render_Mesh,
    Render_Skybox,
    Render_Rect,
    Command_Count,
};


constexpr uint32 CommandCapacity(command_type Type)
{
    switch (Type)
    {
        case Bind_Camera:    return 16;
        case Bind_Light:     return 16;
        case Load_Mesh:      return 256;
        case Load_Texture:   return 32;
        case Load_Cubemap:   return 4;
        case Load_Material:  return 64;
        case Render_Mesh:    return 8192;  //MAX_RENDER_MESH_COMMANDS;
        case Render_Skybox:  return 16;
        case Render_Rect:    return 16384; //MAX_RENDER_RECT_COMMANDS;
        default:             return 0;
    }
}


struct command_render_mesh
{
    command_type   Type;
    Matrix4        Transform;
    uint32         MeshHandle;
    material_params Params;
    void          *PosedVertices;
};

struct command_render_skybox
{
    command_type   Type;
    uint32 CubemapHandle;
};

struct command_render_rect
{
    command_type Type;
    Vector2      Min;
    Vector2      Max;
    Vector4      Color;
    Vector4      UV;
    uint32       TextureSlot;
};

struct command_load_mesh
{
    command_type Type;
    uint32       MeshHandle;
    void        *Vertices;
    uint32       VertexCount;
    uint32      *Indices;
    uint32       IndexCount;
};

struct command_load_texture
{
    command_type   Type;
    uint32         TextureHandle;
    void          *Pixels;
    uint32         Width;
    uint32         Height;
    uint32         SRGB;
    image_format Format;
};

struct command_load_cubemap
{
    command_type   Type;
    uint32         CubemapHandle;
    void          *Pixels;
    uint32         FaceSize;
    image_format Format;
};

struct command_load_material
{
    command_type Type;
    uint32       MaterialHandle;
    material     Description;
};

struct command_bind_camera
{
    command_type Type;
    Matrix4 View;
    Vector3 WorldPosition;
    real32  FovY;
};

struct command_bind_light
{
    command_type Type;
    Vector3 Direction;
};

constexpr inline uint32 CommandSize(command_type Type)
{
    switch (Type)
    {
        case Bind_Camera:        return (uint32)sizeof(command_bind_camera);
        case Bind_Light:         return (uint32)sizeof(command_bind_light);
        case Load_Mesh:          return (uint32)sizeof(command_load_mesh);
        case Load_Texture:       return (uint32)sizeof(command_load_texture);
        case Load_Cubemap:       return (uint32)sizeof(command_load_cubemap);
        case Load_Material:      return (uint32)sizeof(command_load_material);
        case Render_Mesh:        return (uint32)sizeof(command_render_mesh);
        case Render_Skybox:      return (uint32)sizeof(command_render_skybox);
        case Render_Rect:        return (uint32)sizeof(command_render_rect);
    }
    return 0;
}

constexpr inline uint32 CommandAlignment(command_type Type)
{
    switch (Type)
    {
        case Bind_Camera:    return alignof(command_bind_camera);
        case Bind_Light:     return alignof(command_bind_light);
        case Load_Mesh:      return alignof(command_load_mesh);
        case Load_Texture:   return alignof(command_load_texture);
        case Load_Cubemap:   return alignof(command_load_cubemap);
        case Load_Material:  return alignof(command_load_material);
        case Render_Mesh:    return alignof(command_render_mesh);
        case Render_Skybox:  return alignof(command_render_skybox);
        case Render_Rect:    return alignof(command_render_rect);
    }
    return 0;
}

struct mesh_draw_item
{
    uint32 SortKey;
    command_render_mesh* Command;
};

struct render_commands
{
    command_type **Commands[Command_Count];
    mesh_draw_item *MeshCommands;
    uint32 CommandCounts[Command_Count];
    uint32 CommandCapacities[Command_Count];

    uint32 VertexCount;
    uint32 IndexCount;
    uint32 MaterialCount;

    memory_arena CommandArena;
};

inline render_commands InitRenderCommands(void *Memory, uint32 Size, memory_arena *Arena)
{
    render_commands Result = {};
    InitializeArena(&Result.CommandArena, Size, Memory);

    uint32 Capacity;
    for (uint32 Type = 0; Type < Command_Count; ++Type)
    {
        Capacity = CommandCapacity((command_type)Type);
        Result.CommandCapacities[Type] = Capacity;
        Result.Commands[Type] = PushArrayAligned(Arena, Capacity, command_type*, alignof(command_type*));
    }

    Capacity = Result.CommandCapacities[Render_Mesh];
    Result.MeshCommands = PushArrayAligned(Arena, Capacity, mesh_draw_item, alignof(mesh_draw_item));

    return Result;
}

inline void PrepareMeshSortKeys(render_commands *Commands, const material *Materials)
{
    for (uint32 Index = 0; Index < Commands->CommandCounts[Render_Mesh]; ++Index)
    {
        mesh_draw_item *Item = &Commands->MeshCommands[Index];
        const material *Material = &Materials[Item->Command->Params.SharedMatIndex];

        Item->SortKey = ((uint32)Material->Pipeline << 16) | PiplineStateHash(Material->PiplineState);
    }
}

inline void *PushRenderCommand(render_commands *Commands, command_type Type, memory_size ExtraSize = 0)
{
    uint32 Size = CommandSize(Type);

    if (Commands->CommandCounts[Type] >= Commands->CommandCapacities[Type])
    {
        Assert(!"Render command array is full");
        return 0;
    }

    uint32 Alignment = CommandAlignment(Type);
    memory_size RequiredSize = Size + GetAlignmentOffset(&Commands->CommandArena, Alignment);
    memory_size AvailableSize = Commands->CommandArena.Size - Commands->CommandArena.Used;
    if (RequiredSize > AvailableSize || ExtraSize > AvailableSize - RequiredSize)
    {
        Assert(!"Render command arena is full");
        return 0;
    }

    command_type *CmdBase = (command_type *)PushSize_(&Commands->CommandArena, Size + ExtraSize, Alignment);
    *CmdBase = Type;
    uint32 Index = Commands->CommandCounts[Type]++;
    if (Type == Render_Mesh)
    {
        mesh_draw_item *Item = &Commands->MeshCommands[Index];
        Item->SortKey = 0;
        Item->Command = (command_render_mesh *)CmdBase;
    }
    else
    {
        Commands->Commands[Type][Index] = CmdBase;
    }

    return CmdBase;
}

inline void PushBindCamera(render_commands *Commands, Matrix4 View, Vector3 WorldPosition, real32 FovY)
{
    command_bind_camera *cmd = (command_bind_camera *)PushRenderCommand(Commands, Bind_Camera);
    if (cmd)
    {
        cmd->View          = View;
        cmd->WorldPosition = WorldPosition;
        cmd->FovY          = FovY;
    }
}

inline void PushBindLight(render_commands* Commands, Vector3 Direction)
{
    command_bind_light* cmd = (command_bind_light*)PushRenderCommand(Commands, Bind_Light);

    if (cmd)
    {
        cmd->Direction = Direction;
    }
}

inline void PushLoadMesh(render_commands *Commands, uint32 MeshHandle, void *Vertices, uint32 VertexCount, uint32 *Indices, uint32 IndexCount)
{
    command_load_mesh *cmd = (command_load_mesh *)PushRenderCommand(Commands, Load_Mesh);
    if (cmd)
    {
        cmd->MeshHandle  = MeshHandle;
        cmd->Vertices    = Vertices;
        cmd->VertexCount = VertexCount;
        cmd->Indices     = Indices;
        cmd->IndexCount  = IndexCount;

        Commands->VertexCount += VertexCount;
        Commands->IndexCount  += IndexCount;
    }
}

inline void PushLoadTexture(render_commands *Commands, uint32 TextureHandle, void *Pixels, uint32 Width, uint32 Height, uint32 SRGB, image_format Format)
{
    command_load_texture *cmd = (command_load_texture *)PushRenderCommand(Commands, Load_Texture);
    if (cmd)
    {
        cmd->TextureHandle  = TextureHandle;
        cmd->Pixels = Pixels;
        cmd->Width  = Width;
        cmd->Height = Height;
        cmd->SRGB   = SRGB;
        cmd->Format = Format;
    }
}

inline void PushLoadCubemap(render_commands *Commands, uint32 CubemapHandle, void *Pixels, uint32 FaceSize, image_format Format)
{
    command_load_cubemap *cmd = (command_load_cubemap *)PushRenderCommand(Commands, Load_Cubemap);
    if (cmd)
    {
        cmd->CubemapHandle    = CubemapHandle;
        cmd->Pixels   = Pixels;
        cmd->FaceSize = FaceSize;
        cmd->Format   = Format;
    }
}

inline void PushLoadMaterial(render_commands *Commands, uint32 MaterialHandle, const material *Material)
{
    if (MaterialHandle >= CommandCapacity(Load_Material))
    {
        Assert(!"Invalid material handle");
        return;
    }
    command_load_material *cmd = (command_load_material *)PushRenderCommand(Commands, Load_Material);
    if (cmd)
    {
        cmd->MaterialHandle = MaterialHandle;
        cmd->Description = *Material;

        if (MaterialHandle >= Commands->MaterialCount)
        {
            Commands->MaterialCount = MaterialHandle + 1;
        }
    }
}

inline void PushRenderTexturedRect(render_commands *Commands, Vector2 Min, Vector2 Max, Vector4 Color, Vector4 UV, uint32 TextureSlot)
{
    command_render_rect *cmd = (command_render_rect *)PushRenderCommand(Commands, Render_Rect);
    if (cmd)
    {
        cmd->Min         = Min;
        cmd->Max         = Max;
        cmd->Color       = Color;
        cmd->UV          = UV;
        cmd->TextureSlot = TextureSlot;
    }
}

inline void PushRenderRect(render_commands *Commands, Vector2 Min, Vector2 Max, Vector4 Color)
{
    PushRenderTexturedRect(Commands, Min, Max, Color, Vector4(0.0f, 0.0f, 0.0f, 0.0f), TEXTURE_NONE);
}

inline void PushRenderSkybox(render_commands *Commands, uint32 Cubemap)
{
    command_render_skybox *cmd = (command_render_skybox *)PushRenderCommand(Commands, Render_Skybox);
    if (cmd)
    {
        cmd->CubemapHandle = Cubemap;
    }
}

inline void PushRenderPosedMesh(render_commands *Commands, Matrix4 Transform, uint32 Mesh, material_params Params, void *PosedVertices)
{
    if (Params.SharedMatIndex >= CommandCapacity(Load_Material) || (Params.TextureCount && !Params.TextureHandles))
    {
        Assert(!"Invalid mesh material parameters");
        return;
    }

    // Reserve the command and its texture handles together, so failure leaves no partial command.
    memory_size TextureSize = (memory_size)Params.TextureCount * sizeof(uint32);
    command_render_mesh* cmd = (command_render_mesh *)PushRenderCommand(Commands, Render_Mesh, TextureSize);
    if (cmd)
    {
        cmd->Transform      = Transform;
        cmd->MeshHandle     = Mesh;
        cmd->Params         = Params;
        cmd->PosedVertices  = PosedVertices;
        cmd->Params.TextureHandles = 0;
        if (Params.TextureCount)
        {
            uint32 *TextureHandles = (uint32 *)(cmd + 1);
            for (uint32 Index = 0; Index < Params.TextureCount; ++Index)
            {
                TextureHandles[Index] = Params.TextureHandles[Index];
            }
            cmd->Params.TextureHandles = TextureHandles;
        }
    }
}

inline void PushRenderMesh(render_commands *Commands, Matrix4 Transform, uint32 Mesh, material_params Params)
{
    PushRenderPosedMesh(Commands, Transform, Mesh, Params, 0);
}

#endif
