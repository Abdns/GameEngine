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

#define MAX_PACK_ASSETS 64

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

        default:
        {
            Assert(!"unsupported asset type");
        } break;
    }

    Table->Data[Table->Count] = Source.Data;
    Table->Count++;
}

internal void LoadSkyCubemap(enga_asset_table *Table, memory_arena *Arena, const char *Path, const char *Name, uint32 FaceSize)
{
    file_data File = ReadAssetFile(Path);

    loaded_hdr Equirect = ParseHDR(Arena, File.Data, File.Size);
    Win32FreeFileMemory(File.Data);
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

internal void LoadTexture(enga_asset_table *Table, memory_arena *Arena, const char *Path, const char *Name)
{
    file_data File = ReadAssetFile(Path);
    loaded_bitmap Bitmap = ParseTGA(Arena, File.Data, File.Size);
    Win32FreeFileMemory(File.Data);
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

internal const char *ImageExtension(const char *Name)
{
    const char *Extension = "";
    for (const char *At = Name; *At; ++At)
    {
        if (*At == '.') Extension = At;
    }
    return Extension;
}

internal bool32 LoadImages(enga_asset_table *Table, memory_arena *Arena, const char *Directory)
{
    char SearchPath[512];
    uint32 DirectoryLength = (uint32)lstrlenA(Directory);
    if (DirectoryLength + 3 > ArrayCount(SearchPath))
    {
        DebugLog("AssetBuilder: image directory path is too long\n");
        return false;
    }
    uint32 At = AppendString(SearchPath, ArrayCount(SearchPath), 0, Directory);
    AppendString(SearchPath, ArrayCount(SearchPath), At, "\\*");

    WIN32_FIND_DATAA Found;
    HANDLE Search = FindFirstFileA(SearchPath, &Found);
    if (Search == INVALID_HANDLE_VALUE)
    {
        DWORD Error = GetLastError();
        if (Error == ERROR_FILE_NOT_FOUND) return true;
        DebugLog("AssetBuilder: cannot scan '%s' (error %lu)\n", Directory, Error);
        return false;
    }

    char Files[MAX_PACK_ASSETS][MAX_PATH];
    uint32 FileCount = 0;
    do
    {
        if (Found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        const char *Extension = ImageExtension(Found.cFileName);
        if (lstrcmpiA(Extension, ".tga") != 0 && lstrcmpiA(Extension, ".hdr") != 0) continue;

        if (FileCount >= MAX_PACK_ASSETS - Table->Count)
        {
            DebugLog("AssetBuilder: too many images in '%s' (pack limit is %u assets)\n", Directory, MAX_PACK_ASSETS);
            FindClose(Search);
            return false;
        }
        AppendString(Files[FileCount++], MAX_PATH, 0, Found.cFileName);
    } while (FindNextFileA(Search, &Found));

    DWORD SearchError = GetLastError();
    FindClose(Search);
    if (SearchError != ERROR_NO_MORE_FILES)
    {
        DebugLog("AssetBuilder: cannot finish scanning '%s' (error %lu)\n", Directory, SearchError);
        return false;
    }

    // Directory enumeration order is unspecified; keep pack ordering reproducible.
    for (uint32 Index = 1; Index < FileCount; ++Index)
    {
        char FileName[MAX_PATH];
        AppendString(FileName, MAX_PATH, 0, Files[Index]);
        uint32 Insert = Index;
        while (Insert > 0 && lstrcmpA(Files[Insert - 1], FileName) > 0)
        {
            AppendString(Files[Insert], MAX_PATH, 0, Files[Insert - 1]);
            --Insert;
        }
        AppendString(Files[Insert], MAX_PATH, 0, FileName);
    }

    for (uint32 Index = 0; Index < FileCount; ++Index)
    {
        const char *FileName = Files[Index];
        const char *Extension = ImageExtension(FileName);
        uint32 NameLength = (uint32)(Extension - FileName);
        if (!NameLength || NameLength >= ENGA_MAX_ASSET_NAME)
        {
            DebugLog("AssetBuilder: image name '%s' must contain 1 to %u bytes before the extension\n", FileName, ENGA_MAX_ASSET_NAME - 1);
            return false;
        }

        char Name[ENGA_MAX_ASSET_NAME];
        CopySize(NameLength, (void *)FileName, Name);
        Name[NameLength] = 0;
        for (uint32 AssetIndex = 0; AssetIndex < Table->Count; ++AssetIndex)
        {
            asset_descriptor *Entry = &Table->Entries[AssetIndex];
            if (Entry->Type == Asset_Image && StringsAreEqual(Entry->Name, Name))
            {
                DebugLog("AssetBuilder: duplicate image name '%s'\n", Name);
                return false;
            }
        }

        char Path[512];
        if (DirectoryLength + 1 + (uint32)lstrlenA(FileName) + 1 > ArrayCount(Path))
        {
            DebugLog("AssetBuilder: path to image '%s' is too long\n", FileName);
            return false;
        }
        At = AppendString(Path, ArrayCount(Path), 0, Directory);
        At = AppendString(Path, ArrayCount(Path), At, "\\");
        AppendString(Path, ArrayCount(Path), At, FileName);

        if (lstrcmpiA(Extension, ".tga") == 0)
        {
            LoadTexture(Table, Arena, Path, Name);
        }
        else
        {
            // HDR files in this directory are equirectangular environment maps.
            LoadSkyCubemap(Table, Arena, Path, Name, 512);
        }
        DebugLog("AssetBuilder: image '%s' loaded from '%s'\n", Name, Path);
    }
    return true;
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
    if (!LoadImages(&Table, &Arena, "..\\assets\\images")) return 1;
    CreateENGA(&Table, OutPath);

    DebugLog("AssetBuilder: '%s' written (%u assets)\n", OutPath, Table.Count);

    return 0;
}
