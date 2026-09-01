#pragma once

#include "Types.h"
#include "Memory.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "Input.h"
#include "Text.cpp"

#define UI_HASH_SLOT_COUNT 128
#define UI_MAX_ELEMENTS    256

#define UI_LIST_NONE 0xFFFFFFFF

#define UI_STRINGIFY_(Value) #Value
#define UI_STRINGIFY(Value)  UI_STRINGIFY_(Value)
#define UI_CALL_SITE         __FILE__ ":" UI_STRINGIFY(__LINE__)

#define UIID()           UIIDFromSite(UI_CALL_SITE, 0)
#define UIIDIndex(Index) UIIDFromSite(UI_CALL_SITE, (uint64)(Index))

struct rect2
{
    Vector2 Min;
    Vector2 Max;
};

struct ui_id
{
    uint64 Value;
};

enum ui_anchor
{
    UIAnchor_TopLeft = 0,
    UIAnchor_TopRight,
    UIAnchor_BottomLeft,
    UIAnchor_BottomRight,
};

enum ui_interaction_type
{
    UIInteraction_None = 0,
    UIInteraction_Click,
    UIInteraction_ToggleBool,
    UIInteraction_DragReal32,
};

struct ui_interaction
{
    ui_id  ID;
    uint32 Type;
    void  *Target;
    real32 Param;
};

struct ui_style
{
    Vector4 Panel;
    Vector4 Base;
    Vector4 Hot;
    Vector4 Active;
    Vector4 Text;

    real32 Padding;
    real32 Spacing;
};

struct ui_element_state
{
    ui_id  ID;
    uint32 LastTouchedFrame;

    real32 Height;
    real32 Scroll;

    ui_element_state *NextInHash;
};

struct ui_context
{
    mouse_input     *Mouse;
    render_commands *Commands;
    asset_store     *Assets;
    uint32           FontHandle;

    Vector2 ViewportSize;

    ui_interaction Hot;
    ui_interaction NextHot;
    real32         NextHotPriority;
    ui_interaction Active;

    uint32 FrameIndex;

    ui_element_state *HashSlots[UI_HASH_SLOT_COUNT];
    ui_element_state  Elements[UI_MAX_ELEMENTS];
    ui_element_state *FirstFreeElement;
    ui_element_state  DummyElement;
    uint32            ElementCount;

    ui_style Style;
};

struct ui_layout
{
    ui_context *UI;
    ui_id       PanelID;

    Vector2 BaseCorner;
    Vector2 At;

    real32 Width;
    real32 RowHeight;

    bool32 InRow;
    real32 RowStartY;
    real32 RowAtX;
    real32 RowMaxHeight;

    real32 MaxX;
    real32 MaxY;
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

inline ui_id UINullID(void)
{
    ui_id Result = {};

    return Result;
}

inline bool32 UIIDIsValid(ui_id ID)
{
    return ID.Value != 0;
}

inline bool32 UIIDsAreEqual(ui_id A, ui_id B)
{
    return A.Value == B.Value;
}

inline uint64 UIHashMix(uint64 Hash, uint64 Value)
{
    Hash ^= Value + 0x9E3779B97F4A7C15ull + (Hash << 6) + (Hash >> 2);

    return Hash;
}

internal ui_id UIIDFromSite(const char *Site, uint64 Index)
{
    uint64 Hash = 14695981039346656037ull;

    for (const char *At = Site; *At; ++At)
    {
        Hash ^= (uint64)(uint8)*At;
        Hash *= 1099511628211ull;
    }

    ui_id Result;
    Result.Value = UIHashMix(Hash, Index);

    if (!Result.Value)
    {
        Result.Value = 1;
    }

    return Result;
}

internal ui_id UIIDCombine(ui_id Parent, uint64 Index)
{
    ui_id Result;
    Result.Value = UIHashMix(Parent.Value, Index);

    if (!Result.Value)
    {
        Result.Value = 1;
    }

    return Result;
}

inline ui_interaction UINullInteraction(void)
{
    ui_interaction Result = {};

    return Result;
}

inline ui_interaction UIInteraction(ui_id ID, ui_interaction_type Type, void *Target, real32 Param)
{
    ui_interaction Result;
    Result.ID     = ID;
    Result.Type   = (uint32)Type;
    Result.Target = Target;
    Result.Param  = Param;

    return Result;
}

inline bool32 UIInteractionIsValid(ui_interaction Interaction)
{
    return UIIDIsValid(Interaction.ID);
}

inline bool32 UIInteractionsAreEqual(ui_interaction A, ui_interaction B)
{
    return UIIDsAreEqual(A.ID, B.ID) && A.Type == B.Type && A.Target == B.Target;
}

internal ui_element_state *FindOrAddNewUIElementState(ui_context *UI, ui_id ID)
{
    uint32 SlotIndex = HashSlotIndex(UI->HashSlots, ID.Value);

    for (ui_element_state *Found = UI->HashSlots[SlotIndex]; Found; Found = Found->NextInHash)
    {
        if (UIIDsAreEqual(Found->ID, ID))
        {
            Found->LastTouchedFrame = UI->FrameIndex;

            return Found;
        }
    }

    ui_element_state *Element = UI->FirstFreeElement;

    if (Element)
    {
        UI->FirstFreeElement = Element->NextInHash;
    }
    else if (UI->ElementCount < UI_MAX_ELEMENTS)
    {
        Element = UI->Elements + UI->ElementCount++;
    }
    else
    {
        Assert(!"UI element pool is full");

        return &UI->DummyElement;
    }

    ZeroStruct(*Element);

    Element->ID               = ID;
    Element->LastTouchedFrame = UI->FrameIndex;
    Element->NextInHash       = UI->HashSlots[SlotIndex];

    UI->HashSlots[SlotIndex] = Element;

    return Element;
}

internal void UICollectStaleElements(ui_context *UI)
{
    for (uint32 SlotIndex = 0; SlotIndex < ArrayCount(UI->HashSlots); ++SlotIndex)
    {
        ui_element_state **At = UI->HashSlots + SlotIndex;

        while (*At)
        {
            ui_element_state *Element = *At;

            if (Element->LastTouchedFrame == UI->FrameIndex)
            {
                At = &Element->NextInHash;
            }
            else
            {
                *At = Element->NextInHash;

                Element->NextInHash  = UI->FirstFreeElement;
                UI->FirstFreeElement = Element;
            }
        }
    }
}

internal ui_style DefaultUIStyle()
{
    ui_style Style;

    Style.Panel  = Vector4(0.08f, 0.09f, 0.12f, 0.80f);
    Style.Base   = Vector4(0.16f, 0.18f, 0.24f, 0.85f);
    Style.Hot    = Vector4(0.26f, 0.32f, 0.44f, 0.90f);
    Style.Active = Vector4(0.15f, 0.45f, 0.90f, 0.95f);
    Style.Text   = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

    Style.Padding = 6.0f;
    Style.Spacing = 4.0f;

    return Style;
}

internal Vector4 UIWidgetColor(ui_context *UI, ui_interaction Interaction)
{
    if (UIInteractionsAreEqual(UI->Active, Interaction))
    {
        return UI->Style.Active;
    }

    if (UIInteractionsAreEqual(UI->Hot, Interaction))
    {
        return UI->Style.Hot;
    }

    return UI->Style.Base;
}

internal void UIDrawTextIn(ui_context *UI, const char *Text, rect2 Rect, bool32 Centered)
{
    real32 TextX = Rect.Min.X + UI->Style.Padding;

    if (Centered)
    {
        TextX = Rect.Min.X + 0.5f * ((Rect.Max.X - Rect.Min.X) - TextWidth(UI->Assets, UI->FontHandle, Text));
    }

    real32 TextY = Rect.Min.Y + 0.5f * ((Rect.Max.Y - Rect.Min.Y) - TextLineAdvance(UI->Assets, UI->FontHandle));

    DrawText(UI->Commands, UI->Assets, UI->FontHandle, Vector2(TextX, TextY), UI->Style.Text, Text);
}

internal void UIRequestHot(ui_context *UI, ui_interaction Interaction, real32 Priority)
{
    if (Priority >= UI->NextHotPriority)
    {
        UI->NextHot         = Interaction;
        UI->NextHotPriority = Priority;
    }
}

internal void UIApplyInteraction(ui_interaction Interaction)
{
    switch (Interaction.Type)
    {
        case UIInteraction_ToggleBool:
        {
            bool32 *Value = (bool32 *)Interaction.Target;

            *Value = !*Value;
        } break;

        default:
        {
        } break;
    }
}

internal void UIDragActiveInteraction(ui_context *UI)
{
    ui_interaction *Active = &UI->Active;

    switch (Active->Type)
    {
        case UIInteraction_DragReal32:
        {
            real32 *Value = (real32 *)Active->Target;

            *Value += UI->Mouse->Delta.X * Active->Param;
        } break;

        default:
        {
        } break;
    }
}

internal bool32 UIHandleInteraction(ui_context *UI, ui_interaction Interaction, rect2 Rect, real32 Priority)
{
    mouse_input *Mouse = UI->Mouse;

    bool32 Inside = PointInRect(Mouse->Position, Rect);
    if (Inside)
    {
        Mouse->OverUI = true;

        UIRequestHot(UI, Interaction, Priority);
    }

    bool32 Fired = false;

    if (UIInteractionsAreEqual(UI->Active, Interaction))
    {
        Mouse->Consumed = true;

        if (Mouse->Released)
        {
            Fired      = Inside;
            UI->Active = UINullInteraction();
        }
    }
    else if (!UIInteractionIsValid(UI->Active) && UIInteractionsAreEqual(UI->Hot, Interaction) && Mouse->Pressed)
    {
        UI->Active      = Interaction;
        Mouse->Consumed = true;
    }

    if (Fired)
    {
        UIApplyInteraction(Interaction);
    }

    return Fired;
}

internal void BeginUI(ui_context *UI, mouse_input *Mouse, render_commands *Commands, asset_store *Assets, uint32 FontHandle, Vector2 ViewportSize)
{
    UI->Mouse        = Mouse;
    UI->Commands     = Commands;
    UI->Assets       = Assets;
    UI->FontHandle   = FontHandle;
    UI->ViewportSize = ViewportSize;

    UI->NextHot         = UINullInteraction();
    UI->NextHotPriority = -REAL32_LARGE;

    ++UI->FrameIndex;
}

internal void EndUI(ui_context *UI)
{
    if (UIInteractionIsValid(UI->Active))
    {
        UIDragActiveInteraction(UI);
    }

    if (!UI->Mouse->Down)
    {
        UI->Active = UINullInteraction();
    }

    UI->Hot = UI->NextHot;

    UICollectStaleElements(UI);
}

internal real32 UIRowHeight(ui_context *UI)
{
    return TextLineAdvance(UI->Assets, UI->FontHandle) + 2.0f * UI->Style.Padding;
}

internal ui_layout UIBeginPanel_(ui_context *UI, Vector2 Position, real32 Width, ui_id ID)
{
    ui_element_state *State = FindOrAddNewUIElementState(UI, ID);

    real32 PanelWidth = Width + 2.0f * UI->Style.Padding;

    if (State->Height > 0.0f)
    {
        rect2 Background = RectMinDim(Position.X, Position.Y, PanelWidth, State->Height);

        if (PointInRect(UI->Mouse->Position, Background))
        {
            UI->Mouse->OverUI = true;
        }

        PushRenderRect(UI->Commands, Background.Min, Background.Max, UI->Style.Panel);
    }

    ui_layout Layout = {};

    Layout.UI         = UI;
    Layout.PanelID    = ID;
    Layout.BaseCorner = Position;
    Layout.At         = Position + Vector2(UI->Style.Padding, UI->Style.Padding);
    Layout.Width      = Width;
    Layout.RowHeight  = UIRowHeight(UI);
    Layout.MaxX       = Layout.At.X;
    Layout.MaxY       = Layout.At.Y;

    return Layout;
}

#define UIBeginPanel(UI, Position, Width) UIBeginPanel_(UI, Position, Width, UIID())

internal ui_layout UIBeginPanelAnchored_(ui_context *UI, ui_anchor Anchor, Vector2 Margin, real32 Width, ui_id ID)
{
    ui_element_state *State = FindOrAddNewUIElementState(UI, ID);

    real32 PanelWidth  = Width + 2.0f * UI->Style.Padding;
    real32 PanelHeight = State->Height;

    if (PanelHeight <= 0.0f)
    {
        PanelHeight = UIRowHeight(UI) + 2.0f * UI->Style.Padding;
    }

    Vector2 P = Margin;

    if (Anchor == UIAnchor_TopRight || Anchor == UIAnchor_BottomRight)
    {
        P.X = UI->ViewportSize.X - Margin.X - PanelWidth;
    }

    if (Anchor == UIAnchor_BottomLeft || Anchor == UIAnchor_BottomRight)
    {
        P.Y = UI->ViewportSize.Y - Margin.Y - PanelHeight;
    }

    return UIBeginPanel_(UI, P, Width, ID);
}

#define UIBeginPanelAnchored(UI, Anchor, Margin, Width) UIBeginPanelAnchored_(UI, Anchor, Margin, Width, UIID())

internal void UIEndPanel(ui_layout *Layout)
{
    ui_context       *UI    = Layout->UI;
    ui_element_state *State = FindOrAddNewUIElementState(UI, Layout->PanelID);

    State->Height = (Layout->MaxY - Layout->BaseCorner.Y) + UI->Style.Padding;
}

internal void UIBeginRow(ui_layout *Layout)
{
    Layout->InRow        = true;
    Layout->RowStartY    = Layout->At.Y;
    Layout->RowAtX       = Layout->At.X;
    Layout->RowMaxHeight = 0.0f;
}

internal void UIEndRow(ui_layout *Layout)
{
    Layout->InRow = false;
    Layout->At.Y  = Layout->RowStartY + Layout->RowMaxHeight + Layout->UI->Style.Spacing;
}

internal rect2 UINextRect(ui_layout *Layout, real32 Width, real32 Height)
{
    rect2 Result;

    if (Layout->InRow)
    {
        Result = RectMinDim(Layout->RowAtX, Layout->RowStartY, Width, Height);

        Layout->RowAtX       += Width + Layout->UI->Style.Spacing;
        Layout->RowMaxHeight  = Maximum(Layout->RowMaxHeight, Height);
    }
    else
    {
        Result = RectMinDim(Layout->At.X, Layout->At.Y, Width, Height);

        Layout->At.Y += Height + Layout->UI->Style.Spacing;
    }

    Layout->MaxX = Maximum(Layout->MaxX, Result.Max.X);
    Layout->MaxY = Maximum(Layout->MaxY, Result.Max.Y);

    return Result;
}

internal void UILabel(ui_layout *Layout, const char *Text)
{
    rect2 Rect = UINextRect(Layout, Layout->Width, Layout->RowHeight);

    UIDrawTextIn(Layout->UI, Text, Rect, false);
}

internal bool32 UIButton_(ui_layout *Layout, const char *Label, ui_id ID)
{
    ui_context *UI = Layout->UI;

    ui_interaction Interaction = UIInteraction(ID, UIInteraction_Click, 0, 0.0f);

    rect2 Rect = UINextRect(Layout, Layout->Width, Layout->RowHeight);

    bool32 Clicked = UIHandleInteraction(UI, Interaction, Rect, 0.0f);

    PushRenderRect(UI->Commands, Rect.Min, Rect.Max, UIWidgetColor(UI, Interaction));

    UIDrawTextIn(UI, Label, Rect, true);

    return Clicked;
}

#define UIButton(Layout, Label) UIButton_(Layout, Label, UIID())

internal bool32 UICheckBox_(ui_layout *Layout, const char *Label, bool32 *Value, ui_id ID)
{
    ui_context *UI = Layout->UI;

    ui_interaction Interaction = UIInteraction(ID, UIInteraction_ToggleBool, Value, 0.0f);

    rect2 Rect = UINextRect(Layout, Layout->Width, Layout->RowHeight);

    bool32 Toggled = UIHandleInteraction(UI, Interaction, Rect, 0.0f);

    Vector4 Color = UIWidgetColor(UI, Interaction);
    if (*Value)
    {
        Color = Vector4(0.45f * Color.X, 0.45f * Color.Y, 0.45f * Color.Z, Color.W);
    }

    PushRenderRect(UI->Commands, Rect.Min, Rect.Max, Color);

    UIDrawTextIn(UI, Label, Rect, true);

    return Toggled;
}

#define UICheckBox(Layout, Label, Value) UICheckBox_(Layout, Label, Value, UIID())

internal void UIDragReal32_(ui_layout *Layout, const char *Label, real32 *Value, real32 Step, ui_id ID)
{
    ui_context *UI = Layout->UI;

    ui_interaction Interaction = UIInteraction(ID, UIInteraction_DragReal32, Value, Step);

    rect2 Rect = UINextRect(Layout, Layout->Width, Layout->RowHeight);

    UIHandleInteraction(UI, Interaction, Rect, 0.0f);

    PushRenderRect(UI->Commands, Rect.Min, Rect.Max, UIWidgetColor(UI, Interaction));

    UIDrawTextIn(UI, Label, Rect, true);
}

#define UIDragReal32(Layout, Label, Value, Step) UIDragReal32_(Layout, Label, Value, Step, UIID())

internal void UIScrollList_(ui_layout *Layout,uint32 ItemCount, uint32 VisibleRows, ui_id ID)
{
    ui_context       *UI    = Layout->UI;
    ui_element_state *State = FindOrAddNewUIElementState(UI, ID);
    mouse_input      *Mouse = UI->Mouse;

    real32 RowHeight  = Layout->RowHeight;
    real32 ViewHeight = (real32)VisibleRows * RowHeight;

    rect2 ViewRect = UINextRect(Layout, Layout->Width, ViewHeight);

    real32 MaxScroll = Maximum(0.0f, (real32)ItemCount * RowHeight - ViewHeight);

    if (PointInRect(Mouse->Position, ViewRect))
    {
        Mouse->OverUI = true;

        if (Mouse->Wheel)
        {
            State->Scroll -= (real32)Mouse->Wheel * RowHeight;
        }
    }

    State->Scroll = Clamp(0.0f, State->Scroll, MaxScroll);

    UINextRect(Layout, Layout->Width, State->Scroll * Layout->RowHeight);

    PushRenderRect(UI->Commands, ViewRect.Min, ViewRect.Max, UI->Style.Panel);

    uint32 FirstIndex   = (uint32)(State->Scroll / RowHeight);
}

#define UIScrollList(Layout, ItemCount, VisibleRows) UIScrollList_(Layout,ItemCount, VisibleRows, UIID())
