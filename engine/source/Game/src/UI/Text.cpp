#pragma once

#include "RenderCommands.h"

constexpr uint32 FONT_ATLAS_GRID = 16;
constexpr real32 FONT_ADVANCE_RATIO = 28.0f / 64.0f;

struct font_atlas
{
    uint32 TextureHandle;
};

internal void DrawText(render_commands *Commands, font_atlas *Font, const char *Text, Vector2 BottomLeft, real32 Size, Vector4 Color)
{
    real32 UVCellSize = 1.0f / FONT_ATLAS_GRID;
    real32 Advance = Size * FONT_ADVANCE_RATIO;

    Vector2 Pen = BottomLeft;
    while (*Text && Commands->CommandCounts[Render_Rect] < Commands->CommandCapacities[Render_Rect])
    {
        uint32 Code = (uint8)*Text++;
        if (Code == '\r' || Code == '\n')
        {
            if (Code == '\r' && *Text == '\n')
            {
                ++Text;
            } 

            Pen.X = BottomLeft.X;
            Pen.Y += Size;

            continue;
        }

        if (Code == ' ')
        {
            continue;
        }

        if (Code < ' ' || Code > '~')
        {
            Code = '?';
        }

        Vector2 Min = Vector2(Pen.X, Pen.Y - Size);
        Vector2 Max = Vector2(Min.X + Size, Pen.Y);
        Pen.X += Advance;

        if (Max.X <= Min.X || Max.Y <= Min.Y)
        {
            continue;
        }

        uint32 Index = Code - ' ';
        Vector2 UVMin = Vector2((real32)(Index % FONT_ATLAS_GRID), (real32)(Index / FONT_ATLAS_GRID)) * UVCellSize;
        Vector2 UVMax = UVMin + Vector2(UVCellSize, UVCellSize);

        PushRenderTexturedRect(Commands, Min, Max, Color, Vector4(UVMin.X, UVMin.Y, UVMax.X, UVMax.Y), Font->TextureHandle);
    }
}
