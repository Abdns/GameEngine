#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "EngaFormat.h"
#include "RenderCommands.h"
#include "AssetStore.cpp"

internal uint32 NextCodepoint(const char **At)
{
    const uint8 *Bytes = (const uint8 *)*At;

    uint32 Lead = Bytes[0];
    uint32 Extra = 0;
    uint32 Codepoint = Lead;

    if (Lead >= 0xF0)
    {
        Extra = 3;
        Codepoint = Lead & 0x07;
    }
    else if (Lead >= 0xE0)
    {
        Extra = 2;
        Codepoint = Lead & 0x0F;
    }
    else if (Lead >= 0xC0)
    {
        Extra = 1;
        Codepoint = Lead & 0x1F;
    }

    for (uint32 Index = 0; Index < Extra; ++Index)
    {
        uint32 Continuation = Bytes[1 + Index];
        if ((Continuation & 0xC0) != 0x80)
        {
            Extra = Index;
            break;
        }

        Codepoint = (Codepoint << 6) | (Continuation & 0x3F);
    }

    *At += 1 + Extra;

    return Codepoint;
}

internal void DrawText(render_commands *Commands, asset_store *Assets, uint32 FontHandle, Vector2 P, Vector4 Color, const char *Text)
{
    if (FontHandle >= Assets->FontCount)
    {
        return;
    }

    uint32 Slot = FontHandle;

    asset_font_info *Font     = Assets->FontInfo + Slot;
    uint16          *Map      = AssetFontMap(Assets, Slot);
    real32          *Advances = AssetFontAdvance(Assets, Slot);
    uint32           Texture  = Assets->FontTextureHandle[Slot];
    uint32           Columns  = Font->AtlasSize / Font->CellWidth;
    real32           InvSize  = 1.0f / (real32)Font->AtlasSize;

    real32 PenX = P.X;

    for (const char *At = Text; *At;)
    {
        uint32 Codepoint = NextCodepoint(&At);
        if (Codepoint >= ENGA_MAX_CODEPOINT)
        {
            continue;
        }

        uint32 Cell = Map[Codepoint];
        if (Cell)
        {
            real32 U0 = (real32)((Cell % Columns) * Font->CellWidth)  * InvSize;
            real32 V0 = (real32)((Cell / Columns) * Font->CellHeight) * InvSize;

            Vector2 Min = Vector2(PenX - Font->OriginX, P.Y);
            Vector2 Max = Vector2(Min.X + (real32)Font->CellWidth, Min.Y + (real32)Font->CellHeight);

            Vector4 UV = Vector4(U0, V0, U0 + (real32)Font->CellWidth * InvSize, V0 + (real32)Font->CellHeight * InvSize);

            PushRenderTexturedRect(Commands, Min, Max, Color, UV, Texture);
        }

        PenX += Advances[Codepoint];
    }
}

internal real32 TextWidth(asset_store *Assets, uint32 FontHandle, const char *Text)
{
    real32 Width = 0.0f;

    if (FontHandle >= Assets->FontCount)
    {
        return Width;
    }

    real32 *Advances = AssetFontAdvance(Assets, FontHandle);

    for (const char *At = Text; *At;)
    {
        uint32 Codepoint = NextCodepoint(&At);
        if (Codepoint < ENGA_MAX_CODEPOINT)
        {
            Width += Advances[Codepoint];
        }
    }

    return Width;
}

internal real32 TextLineAdvance(asset_store *Assets, uint32 FontHandle)
{
    if (FontHandle >= Assets->FontCount)
    {
        return 0.0f;
    }

    return Assets->FontInfo[FontHandle].LineAdvance;
}
