#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "Memory.h"
#include "Strings.h"
#include "EngaFormat.h"
#include "PlatformAPI.h"
#include "AssetStore.cpp"
#include "Material.cpp"
#include "SimEntity.cpp"

#define MAX_ENTITY_PRESETS 32
#define PRESET_MAX_NAME    ENGA_MAX_ASSET_NAME
#define PRESET_NONE        0xFFFFFFFF

#define PRESET_MAGIC   (((uint32)'E') | ((uint32)'N' << 8) | ((uint32)'G' << 16) | ((uint32)'P' << 24))
#define PRESET_VERSION 1
#define PRESET_PATH    ENGA_ASSET_DIR "\\presets.engp"

struct entity_preset
{
    char Name[PRESET_MAX_NAME];
    char MeshName[PRESET_MAX_NAME];
    char TextureName[PRESET_MAX_NAME];

    uint32    Type;
    bool32    Static;
    transform Pose;
    material  Material;
};

struct preset_file_header
{
    uint32 Magic;
    uint32 Version;
    uint32 Count;
    uint32 PresetSize;
};

struct preset_file
{
    preset_file_header Header;
    entity_preset      Presets[MAX_ENTITY_PRESETS];
};

struct preset_table
{
    uint32        Count;
    entity_preset Presets[MAX_ENTITY_PRESETS];

    uint32 MeshHandles[MAX_ENTITY_PRESETS];
    uint32 MaterialHandles[MAX_ENTITY_PRESETS];
};

internal entity_preset MakePreset(const char *Name, entity_type Type, const char *MeshName, const char *TextureName, transform Pose, bool32 Static, material Material)
{
    entity_preset Result = {};

    AppendString(Result.Name, PRESET_MAX_NAME, 0, Name);
    AppendString(Result.MeshName, PRESET_MAX_NAME, 0, MeshName);

    if (TextureName)
    {
        AppendString(Result.TextureName, PRESET_MAX_NAME, 0, TextureName);
    }

    Result.Type     = (uint32)Type;
    Result.Static   = Static;
    Result.Pose     = Pose;
    Result.Material = Material;

    Result.Material.TextureHandle = 0;

    return Result;
}

internal uint32 AddPreset(preset_table *Table, entity_preset Preset)
{
    Assert(Table->Count < MAX_ENTITY_PRESETS);

    uint32 Index = Table->Count++;

    Table->Presets[Index]         = Preset;
    Table->MeshHandles[Index]     = ASSET_HANDLE_NONE;
    Table->MaterialHandles[Index] = ASSET_HANDLE_NONE;

    return Index;
}

internal uint32 GetPresetIndex(preset_table *Table, const char *Name)
{
    for (uint32 Index = 0; Index < Table->Count; ++Index)
    {
        if (StringsAreEqual(Table->Presets[Index].Name, Name))
        {
            return Index;
        }
    }

    return PRESET_NONE;
}

internal void LinkPresets(preset_table *Table, asset_store *Assets, materials *Materials)
{
    for (uint32 Index = 0; Index < Table->Count; ++Index)
    {
        entity_preset *Preset = Table->Presets + Index;

        material Material = Preset->Material;

        if (Preset->TextureName[0])
        {
            Material.TextureHandle = GetAssetTextureHandle(Assets, Preset->TextureName);
        }

        Table->MeshHandles[Index]     = GetAssetMeshHandle(Assets, Preset->MeshName);
        Table->MaterialHandles[Index] = AddMaterial(Materials, Material);
    }
}

internal bool32 LoadPresets(preset_table *Table, game_memory *Memory, const char *Path)
{
    file_data File = Memory->PlatformReadEntireFile(Path);
    if (!File.Data)
    {
        DebugLog("Presets: '%s' is missing\n", Path);

        return false;
    }

    bool32 Loaded = false;

    preset_file_header *Header = (preset_file_header *)File.Data;

    uint32 ExpectedSize = (uint32)sizeof(preset_file_header);
    if (File.Size >= ExpectedSize)
    {
        ExpectedSize += Header->Count * (uint32)sizeof(entity_preset);
    }

    if (File.Size >= ExpectedSize &&
        Header->Magic      == PRESET_MAGIC &&
        Header->Version    == PRESET_VERSION &&
        Header->PresetSize == sizeof(entity_preset) &&
        Header->Count      <= MAX_ENTITY_PRESETS)
    {
        ZeroStruct(*Table);

        CopySize((memory_size)Header->Count * sizeof(entity_preset), Header + 1, Table->Presets);
        Table->Count = Header->Count;

        for (uint32 Index = 0; Index < Table->Count; ++Index)
        {
            Table->MeshHandles[Index]     = ASSET_HANDLE_NONE;
            Table->MaterialHandles[Index] = ASSET_HANDLE_NONE;
        }

        Loaded = true;

        DebugLog("Presets: %u loaded from '%s'\n", Table->Count, Path);
    }
    else
    {
        DebugLog("Presets: '%s' is not a version %u preset file\n", Path, PRESET_VERSION);
    }

    Memory->PlatformFreeFileMemory(File.Data);

    return Loaded;
}

#if ENGINE_INTERNAL

internal bool32 SavePresets(preset_table *Table, game_memory *Memory, const char *Path)
{
    Assert(Table->Count <= MAX_ENTITY_PRESETS);

    preset_file File = {};

    File.Header.Magic      = PRESET_MAGIC;
    File.Header.Version    = PRESET_VERSION;
    File.Header.Count      = Table->Count;
    File.Header.PresetSize = (uint32)sizeof(entity_preset);

    CopySize((memory_size)Table->Count * sizeof(entity_preset), Table->Presets, File.Presets);

    for (uint32 Index = 0; Index < Table->Count; ++Index)
    {
        File.Presets[Index].Material.TextureHandle = 0;
    }

    uint32 WriteSize = (uint32)sizeof(preset_file_header) + Table->Count * (uint32)sizeof(entity_preset);

    bool32 Wrote = Memory->DEBUGPlatformWriteEntireFile((char *)Path, WriteSize, &File);

    DebugLog("Presets: %u written to '%s' (%u bytes)\n", Table->Count, Path, WriteSize);

    return Wrote;
}

#endif
