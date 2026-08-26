#include <windows.h>

#include "Types.h"
#include "Memory.h"
#include "Strings.h"
#include "Win32FileIO.h"
#include "EngaFormat.h"
#include "Loaders/GLTF.h"
#include "Loaders/TGA.h"
#include "Loaders/HDR.h"
#include "Loaders/Cubemap.h"
#include "Loaders/Font.h"

#define MAX_PACK_ASSETS 64
#define MAX_FONT_GLYPHS 512

struct enga_asset_table
{
    asset_descriptor Entries[MAX_PACK_ASSETS];
    void       *Data[MAX_PACK_ASSETS];
    uint32      Count;
};

struct asset_source
{
    asset_type  Type;
    const char *Name;
    void       *Data;
    union
    {
        asset_mesh_info  Mesh;
        asset_image_info Image;
        asset_font_info  Font;
    };
};

internal file_data ReadAssetFile(const char *Path)
{
    file_data Result = Win32ReadEntireFile(Path);
    if (!Result.Data)
    {
        DebugLog("AssetBuilder: cannot read '%s'\n", Path);
        Assert(Result.Data);
    }

    return Result;
}

internal file_data ReadRelativeFile(const char *BasePath, const char *Uri)
{
    char Path[512];
    uint32 DirLength = 0;
    for (uint32 i = 0; BasePath[i]; ++i)
    {
        if (BasePath[i] == '\\' || BasePath[i] == '/')
        {
            DirLength = i + 1;
        }
    }

    uint32 At = 0;
    for (; At < DirLength && At < (uint32)ArrayCount(Path) - 1; ++At)
    {
        Path[At] = BasePath[At];
    }
    Path[At] = 0;
    AppendString(Path, (uint32)ArrayCount(Path), At, Uri);

    return ReadAssetFile(Path);
}

internal void AddAsset(enga_asset_table *Table, asset_source Source)
{
    Assert(Table->Count < MAX_PACK_ASSETS);
    Assert(Source.Name);
    Assert(Source.Data);

    asset_descriptor *Entry = &Table->Entries[Table->Count];
    *Entry = {};
    Entry->Type = (uint32)Source.Type;
    AppendString(Entry->Name, ENGA_MAX_ASSET_NAME, 0, Source.Name);

    switch (Source.Type)
    {
        case Asset_Mesh:
        {
            Assert(Source.Mesh.VertexCount);

            Entry->Mesh = Source.Mesh;
            Entry->Size = (uint64)Source.Mesh.VertexCount * sizeof(enga_vertex) +
                          (uint64)Source.Mesh.IndexCount * sizeof(uint32);
        } break;

        case Asset_Image:
        {
            Assert(Source.Image.Width && Source.Image.Height && Source.Image.Layers);

            Entry->Image = Source.Image;
            Entry->Size  = (uint64)Source.Image.Width * Source.Image.Height * Source.Image.Layers *
                           AssetImageFormatBytes((asset_image_format)Source.Image.Format);
        } break;

        case Asset_Font:
        {
            Assert(Source.Font.AtlasSize && Source.Font.CellWidth && Source.Font.CellHeight);

            Entry->Font = Source.Font;
            Entry->Size = (uint64)ENGA_MAX_CODEPOINT * (sizeof(uint16) + sizeof(real32)) +
                          (uint64)Source.Font.AtlasSize * Source.Font.AtlasSize *
                          AssetImageFormatBytes(ImageFormat_RGBA8);
        } break;

        default:
        {
            Assert(!"unsupported asset type");
        } break;
    }

    Table->Data[Table->Count] = Source.Data;
    Table->Count++;
}

internal void LoadFont(enga_asset_table *Table, memory_arena *Arena, const char *Path, const char *Name, real32 PixelHeight, uint32 AtlasSize)
{
    file_data File = ReadAssetFile(Path);

    int32 Ranges[][2] =
    {
        {   32,  126 },
        { 1025, 1025 },
        { 1040, 1103 },
        { 1105, 1105 },
    };

    int32  Codepoints[MAX_FONT_GLYPHS];
    uint32 Count = 0;
    for (uint32 RangeIndex = 0; RangeIndex < ArrayCount(Ranges); ++RangeIndex)
    {
        for (int32 Codepoint = Ranges[RangeIndex][0]; Codepoint <= Ranges[RangeIndex][1]; ++Codepoint)
        {
            Assert(Count < ArrayCount(Codepoints));
            Codepoints[Count++] = Codepoint;
        }
    }

    loaded_font Font = BakeFont(Arena, File.Data, PixelHeight, Codepoints, Count, AtlasSize);

    asset_source FontAsset = {};
    FontAsset.Type = Asset_Font;
    FontAsset.Name = Name;
    FontAsset.Data = Font.Blob;
    FontAsset.Font = Font.Info;
    AddAsset(Table, FontAsset);
}

internal void LoadSkyCubemap(enga_asset_table *Table, memory_arena *Arena, const char *Path, const char *Name, uint32 FaceSize)
{
    file_data File = ReadAssetFile(Path);

    loaded_hdr Equirect = ParseHDR(Arena, File.Data, File.Size);
    Assert(Equirect.Pixels);

    loaded_cubemap Cube = EquirectToCubemap(Arena, &Equirect, FaceSize);
    Assert(Cube.Pixels);

    asset_source Cubemap = {};
    Cubemap.Type         = Asset_Image;
    Cubemap.Name         = Name;
    Cubemap.Data         = Cube.Pixels;
    Cubemap.Image.Format = ImageFormat_RGBA16F;
    Cubemap.Image.Width  = Cube.FaceSize;
    Cubemap.Image.Height = Cube.FaceSize;
    Cubemap.Image.Layers = 6;
    AddAsset(Table, Cubemap);
}

internal void LoadGLTF(enga_asset_table *Table, memory_arena *Arena, const char *Path)
{
    file_data File = ReadAssetFile(Path);

    gltf_file Gltf = ParseGLTF(Arena, File.Data, File.Size);
    Assert(Gltf.Root);

    if (!Gltf.Bin)
    {
        char *Uri = JsonCString(JsonGet(JsonAt(JsonGet(Gltf.Root, "buffers"), 0), "uri"));
        Assert(Uri);

        file_data Bin = ReadRelativeFile(Path, Uri);

        Gltf.Bin     = (uint8 *)Bin.Data;
        Gltf.BinSize = Bin.Size;
    }

    json_value *Meshes = JsonGet(Gltf.Root, "meshes");
    for (json_member *Member = Meshes ? Meshes->First : 0; Member; Member = Member->Next)
    {
        json_value *Mesh = Member->Value;

        char *Name = JsonCString(JsonGet(Mesh, "name"));
        Assert(Name);

        gltf_geometry Geometry = GLTFMeshGeometry(Arena, &Gltf, Mesh);
        Assert(Geometry.Blob);

        asset_source MeshAsset = {};
        MeshAsset.Type             = Asset_Mesh;
        MeshAsset.Name             = Name;
        MeshAsset.Data             = Geometry.Blob;
        MeshAsset.Mesh.VertexCount = Geometry.VertexCount;
        MeshAsset.Mesh.IndexCount  = Geometry.IndexCount;
        AddAsset(Table, MeshAsset);
    }

    json_value *Images = JsonGet(Gltf.Root, "images");
    for (json_member *Member = Images ? Images->First : 0; Member; Member = Member->Next)
    {
        json_value *Image = Member->Value;

        char *Name = JsonCString(JsonGet(Image, "name"));
        char *Uri  = JsonCString(JsonGet(Image, "uri"));
        Assert(Name);
        Assert(Uri);

        file_data ImageFile = ReadRelativeFile(Path, Uri);

        loaded_bitmap Bitmap = ParseTGA(Arena, ImageFile.Data, ImageFile.Size);
        Assert(Bitmap.Pixels);

        asset_source Texture = {};
        Texture.Type         = Asset_Image;
        Texture.Name         = Name;
        Texture.Data         = Bitmap.Pixels;
        Texture.Image.Format = ImageFormat_RGBA8;
        Texture.Image.IsSRGB = true;
        Texture.Image.Width  = Bitmap.Width;
        Texture.Image.Height = Bitmap.Height;
        Texture.Image.Layers = 1;
        AddAsset(Table, Texture);
    }
}

internal void CreateENGA(enga_asset_table *Table, const char *Path)
{
    asset_file_header Header = {};
    Header.Magic            = ENGA_MAGIC;
    Header.Version          = ENGA_VERSION;
    Header.AssetCount       = Table->Count;
    Header.AssetTableOffset = (uint32)sizeof(asset_file_header);

    uint64 DataOffset = sizeof(asset_file_header) + (uint64)Table->Count * sizeof(asset_descriptor);
    for (uint32 i = 0; i < Table->Count; ++i)
    {
        Table->Entries[i].Offset = DataOffset;
        DataOffset += Table->Entries[i].Size;
    }

    HANDLE File = CreateFileA(Path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    Assert(File != INVALID_HANDLE_VALUE);

    DWORD Written = 0;

    BOOL Ok = WriteFile(File, &Header, (DWORD)sizeof(Header), &Written, 0);
    Assert(Ok);

    Ok = WriteFile(File, Table->Entries, (DWORD)(Table->Count * sizeof(asset_descriptor)), &Written, 0);
    Assert(Ok);

    for (uint32 i = 0; i < Table->Count; ++i)
    {
        Ok = WriteFile(File, Table->Data[i], (DWORD)Table->Entries[i].Size, &Written, 0);
        Assert(Ok);
    }

    CloseHandle(File);
}

int main(int ArgCount, char **Args)
{
    const char *OutPath = (ArgCount > 1) ? Args[1] : ENGA_PACK_PATH;

    uint32 ArenaSize = (uint32)Megabytes(64);
    void  *ArenaMemory = VirtualAlloc(0, ArenaSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Assert(ArenaMemory);

    memory_arena Arena;
    InitializeArena(&Arena, ArenaSize, ArenaMemory);

    enga_asset_table Table = {};

    LoadGLTF(&Table, &Arena, "..\\assets\\models\\TestShapes\\TestShapes.gltf");
    LoadGLTF(&Table, &Arena, "..\\assets\\models\\Gizmo\\Gizmo.gltf");
    LoadSkyCubemap(&Table, &Arena, "..\\assets\\images\\sky.hdr", "sky", 512);
    LoadFont(&Table, &Arena, "..\\assets\\fonts\\DejaVuSansMono.ttf", "DejaVuSansMono24", 24.0f, 256);
    CreateENGA(&Table, OutPath);

    DebugLog("AssetBuilder: '%s' written (%u assets)\n", OutPath, Table.Count);

    return 0;
}
