#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "Input.h"
#include "Text.cpp"

struct rect2
{
    Vector2 Min;
    Vector2 Max;
};

struct ui_style
{
    Vector4 Base;
    Vector4 Hot;
    Vector4 Active;
};

struct ui_context
{
    mouse_input *Mouse;

    uint32 Hot;
    uint32 Active;

    ui_style         Style;
    render_commands *Commands;
};

inline rect2 RectMinMax(real32 MinX, real32 MinY, real32 MaxX, real32 MaxY)
{
    rect2 Result;

    Result.Min = Vector2(MinX, MinY);
    Result.Max = Vector2(MaxX, MaxY);

    return Result;
}

inline rect2 RectMinDim(real32 MinX, real32 MinY, real32 Width, real32 Height)
{
    return RectMinMax(MinX, MinY, MinX + Width, MinY + Height);
}

inline bool32 PointInRect(Vector2 Point, rect2 Rect)
{
    return (Point.X >= Rect.Min.X && Point.X < Rect.Max.X && Point.Y >= Rect.Min.Y && Point.Y < Rect.Max.Y);
}

internal ui_style DefaultUIStyle()
{
    ui_style Style;

    Style.Base   = Vector4(0.16f, 0.18f, 0.24f, 0.85f);
    Style.Hot    = Vector4(0.26f, 0.32f, 0.44f, 0.90f);
    Style.Active = Vector4(0.15f, 0.45f, 0.90f, 0.95f);

    return Style;
}

internal void BeginUI(ui_context *UI, mouse_input *Mouse, render_commands *Commands)
{
    UI->Mouse    = Mouse;
    UI->Hot      = 0;
    UI->Commands = Commands;
}

internal void EndUI(ui_context *UI)
{
    if (!UI->Mouse->Down)
    {
        UI->Active = 0;
    }
}

internal Vector4 UIWidgetColor(ui_context *UI, uint32 ID)
{
    if (UI->Active == ID)
    {
        return UI->Style.Active;
    }

    if (UI->Hot == ID)
    {
        return UI->Style.Hot;
    }

    return UI->Style.Base;
}

internal bool32 UIWidgetInput(ui_context *UI, uint32 ID, rect2 Rect)
{
    mouse_input *Mouse = UI->Mouse;

    bool32 Clicked = false;
    bool32 Inside  = PointInRect(Mouse->Position, Rect);

    if (UI->Active == ID)
    {
        Mouse->Consumed = true;

        if (Mouse->Released)
        {
            Clicked = Inside;
            UI->Active = 0;
        }
    }
    else if (!UI->Active && Inside && !Mouse->Consumed)
    {
        UI->Hot         = ID;
        Mouse->Consumed = true;

        if (Mouse->Pressed)
        {
            UI->Active = ID;
        }
    }

    return Clicked;
}

internal bool32 UIButton(ui_context *UI, uint32 ID, rect2 Rect)
{
    bool32 Clicked = UIWidgetInput(UI, ID, Rect);

    PushRenderRect(UI->Commands, Rect.Min, Rect.Max, UIWidgetColor(UI, ID));

    return Clicked;
}

internal void UIPanel(ui_context *UI, rect2 Rect, Vector4 Color)
{
    if (PointInRect(UI->Mouse->Position, Rect))
    {
        UI->Mouse->OverUI = true;
    }

    PushRenderRect(UI->Commands, Rect.Min, Rect.Max, Color);
}

internal uint32 UIIDFromString(const char *Name)
{
    uint32 Hash = 2166136261u;

    for (const char *At = Name; *At; ++At)
    {
        Hash ^= (uint32)(uint8)*At;
        Hash *= 16777619u;
    }

    return Hash ? Hash : 1;
}

internal bool32 UILabeledButton(ui_context *UI, asset_store *Assets, uint32 FontHandle, const char *Label, rect2 Rect)
{
    bool32 Clicked = UIButton(UI, UIIDFromString(Label), Rect);

    Vector2 LabelP = Vector2(Rect.Min.X + 0.5f * ((Rect.Max.X - Rect.Min.X) - TextWidth(Assets, FontHandle, Label)),
                             Rect.Min.Y + 0.5f * ((Rect.Max.Y - Rect.Min.Y) - TextLineAdvance(Assets, FontHandle)));

    DrawText(UI->Commands, Assets, FontHandle, LabelP, Vector4(1.0f, 1.0f, 1.0f, 1.0f), Label);

    return Clicked;
}

internal bool32 UILabledCheckBox(ui_context *UI, asset_store *Assets, uint32 FontHandle, const char *Label, rect2 Rect, bool32 *Value)
{
    uint32 ID = UIIDFromString(Label);

    bool32 Clicked = UIWidgetInput(UI, ID, Rect);
    if (Clicked)
    {
        *Value = !*Value;
    }

    Vector4 Color = UIWidgetColor(UI, ID);
    if (*Value)
    {
        Color = Vector4(0.45f * Color.X, 0.45f * Color.Y, 0.45f * Color.Z, Color.W);
    }

    PushRenderRect(UI->Commands, Rect.Min, Rect.Max, Color);

    Vector2 LabelP = Vector2(Rect.Min.X + 0.5f * ((Rect.Max.X - Rect.Min.X) - TextWidth(Assets, FontHandle, Label)), Rect.Min.Y + 0.5f * ((Rect.Max.Y - Rect.Min.Y) - TextLineAdvance(Assets, FontHandle)));

    DrawText(UI->Commands, Assets, FontHandle, LabelP, Vector4(1.0f, 1.0f, 1.0f, 1.0f), Label);

    return Clicked;
}

internal bool32 UIScrollList(ui_context* UI, asset_store* Assets, uint32 FontHandle, const char* Label, rect2 Rect)
{
    uint32 ID = UIIDFromString(Label);
}

internal bool32 UIPropertyList()
{

}
