#ifndef FONT_H
#define FONT_H

#include <windows.h>

#include "Types.h"
#include "Memory.h"
#include "EngineMath.h"
#include "EngaFormat.h"

struct glyph_metrics
{
    bool32 Valid;
    int32  X0, Y0;
    int32  Width, Height;
    int32  Advance;
};

struct loaded_font
{
    void            *Blob;
    uint16          *Map;
    real32          *Advances;
    uint8           *Pixels;
    asset_font_info  Info;
};

internal loaded_font BakeFont(memory_arena *Arena, const char *Path, const char *FaceName, int32 PixelHeight, int32 *Codepoints, uint32 CodepointCount, uint32 AtlasSize)
{
    loaded_font Result = {};

    Assert(Path && FaceName && PixelHeight > 0 && Codepoints && CodepointCount && AtlasSize);

    if (!AddFontResourceExA(Path, FR_PRIVATE, 0))
    {
        DebugLog("BakeFont: cannot load '%s'\n", Path);
        Assert(!"font file missing");
    }

    HDC DC = CreateCompatibleDC(0);
    Assert(DC);

    HFONT GdiFont = CreateFontA(PixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                                ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, FaceName);
    Assert(GdiFont);

    HGDIOBJ OldFont = SelectObject(DC, GdiFont);

    TEXTMETRICA TextMetrics = {};
    GetTextMetricsA(DC, &TextMetrics);

    MAT2 Transform = {};
    Transform.eM11.value = 1;
    Transform.eM22.value = 1;

    memory_size MapBytes     = ENGA_MAX_CODEPOINT * sizeof(uint16);
    memory_size AdvanceBytes = ENGA_MAX_CODEPOINT * sizeof(real32);
    memory_size PixelBytes   = (memory_size)AtlasSize * AtlasSize * 4;

    uint8  *Blob     = (uint8 *)PushSize(Arena, MapBytes + AdvanceBytes + PixelBytes);
    uint16 *Map      = (uint16 *)Blob;
    real32 *Advances = (real32 *)(Blob + MapBytes);
    uint8  *Pixels   = Blob + MapBytes + AdvanceBytes;

    ZeroSize(MapBytes + AdvanceBytes, Blob);

    temporary_memory Temp = BeginTemporaryMemory(Arena);

    glyph_metrics *Glyphs = PushArray(Arena, CodepointCount, glyph_metrics);

    int32  MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
    uint32 MaxGlyphBytes = 0;

    for (uint32 Index = 0; Index < CodepointCount; ++Index)
    {
        glyph_metrics *Glyph = Glyphs + Index;
        *Glyph = {};

        GLYPHMETRICS Metrics = {};
        DWORD Bytes = GetGlyphOutlineW(DC, (UINT)Codepoints[Index], GGO_GRAY8_BITMAP, &Metrics, 0, 0, &Transform);
        if (Bytes == GDI_ERROR)
        {
            DebugLog("BakeFont: '%s' has no glyph for codepoint %d\n", FaceName, Codepoints[Index]);
            continue;
        }

        Glyph->Valid   = true;
        Glyph->X0      = Metrics.gmptGlyphOrigin.x;
        Glyph->Y0      = -Metrics.gmptGlyphOrigin.y;
        Glyph->Width   = Bytes ? (int32)Metrics.gmBlackBoxX : 0;
        Glyph->Height  = Bytes ? (int32)Metrics.gmBlackBoxY : 0;
        Glyph->Advance = Metrics.gmCellIncX;

        MinX = Minimum(MinX, Glyph->X0);
        MinY = Minimum(MinY, Glyph->Y0);
        MaxX = Maximum(MaxX, Glyph->X0 + Glyph->Width);
        MaxY = Maximum(MaxY, Glyph->Y0 + Glyph->Height);

        MaxGlyphBytes = Maximum(MaxGlyphBytes, (uint32)Bytes);
    }

    int32 CellWidth  = MaxX - MinX;
    int32 CellHeight = MaxY - MinY;
    Assert(CellWidth > 0 && CellHeight > 0);

    uint32 Columns = AtlasSize / (uint32)CellWidth;
    uint32 Rows    = Columns ? ((CodepointCount + Columns) / Columns) : 0;
    if (!Columns || Rows * (uint32)CellHeight > AtlasSize)
    {
        DebugLog("BakeFont: %u cells of %dx%d do not fit into a %ux%u atlas\n", CodepointCount, CellWidth, CellHeight, AtlasSize, AtlasSize);
        Assert(!"font atlas too small");
    }

    uint8 *Alpha = PushArray(Arena, (memory_size)AtlasSize * AtlasSize, uint8);
    ZeroSize((memory_size)AtlasSize * AtlasSize, Alpha);

    uint8 *GlyphPixels = PushArray(Arena, MaxGlyphBytes, uint8);

    for (uint32 Index = 0; Index < CodepointCount; ++Index)
    {
        glyph_metrics *Glyph = Glyphs + Index;
        if (!Glyph->Valid)
        {
            continue;
        }

        int32 Codepoint = Codepoints[Index];
        Assert(Codepoint > 0 && Codepoint < ENGA_MAX_CODEPOINT);

        uint32 Cell  = Index + 1;
        int32  CellX = (int32)((Cell % Columns) * (uint32)CellWidth);
        int32  CellY = (int32)((Cell / Columns) * (uint32)CellHeight);

        Map[Codepoint]      = (uint16)Cell;
        Advances[Codepoint] = (real32)Glyph->Advance;

        if (!Glyph->Width || !Glyph->Height)
        {
            continue;
        }

        GLYPHMETRICS Metrics = {};
        DWORD Bytes = GetGlyphOutlineW(DC, (UINT)Codepoint, GGO_GRAY8_BITMAP, &Metrics, MaxGlyphBytes, GlyphPixels, &Transform);
        Assert(Bytes != GDI_ERROR);

        int32 Pitch = (Glyph->Width + 3) & ~3;

        for (int32 Y = 0; Y < Glyph->Height; ++Y)
        {
            uint8 *Source = GlyphPixels + (memory_size)Y * Pitch;
            uint8 *Dest   = Alpha + (memory_size)(CellY - MinY + Glyph->Y0 + Y) * AtlasSize + (CellX - MinX + Glyph->X0);

            for (int32 X = 0; X < Glyph->Width; ++X)
            {
                uint32 Coverage = Minimum((uint32)Source[X], 64u);

                Dest[X] = (uint8)((Coverage * 255 + 32) / 64);
            }
        }
    }

    for (memory_size Index = 0; Index < (memory_size)AtlasSize * AtlasSize; ++Index)
    {
        uint8 *Out = Pixels + Index * 4;
        Out[0] = 255;
        Out[1] = 255;
        Out[2] = 255;
        Out[3] = Alpha[Index];
    }

    EndTemporaryMemory(Temp);

    SelectObject(DC, OldFont);
    DeleteObject(GdiFont);
    DeleteDC(DC);
    RemoveFontResourceExA(Path, FR_PRIVATE, 0);

    Result.Blob             = Blob;
    Result.Map              = Map;
    Result.Advances         = Advances;
    Result.Pixels           = Pixels;
    Result.Info.AtlasSize   = AtlasSize;
    Result.Info.CellWidth   = (uint32)CellWidth;
    Result.Info.CellHeight  = (uint32)CellHeight;
    Result.Info.OriginX     = (real32)-MinX;
    Result.Info.LineAdvance = (real32)(TextMetrics.tmHeight + TextMetrics.tmExternalLeading);

    return Result;
}

#endif
