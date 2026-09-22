#pragma once

#include "Types.h"
#include "Memory.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "Input.h"

#define UI_SCROLL_STATE_CAPACITY 32
#define UI_SCROLL_WHEEL_STEP     36.0f
#define UI_SCROLL_PADDING        4.0f
#define UI_ROW_SPACING           4.0f

enum class ui_element_type
{
	Panel,
	Button
};

struct ui_element
{
	ui_element_type Type;
	rect2 Rect;
	Vector4 Color;
};

struct ui_element_state
{
	uint64 ID;
	real32 Scroll;
	real32 ContentHeight;
};

struct ui_style
{
	Vector4 Panel;
	Vector4 WidgetColor;
	Vector4 HotColor;
	Vector4 ActiveColor;
};

struct ui_context
{
	render_commands *Cmd;
	mouse_input *Mouse;
	real32 DpiRatio;
	ui_style Style;

	uint64 ActiveID;
	uint32 ScrollStateCount;
	ui_element_state ScrollStates[UI_SCROLL_STATE_CAPACITY];
	ui_element_state OverflowState;
};

struct ui_scroll_list
{
	ui_context *UI;
	ui_element_state *State;
	uint64 ID;
	rect2 View;
	rect2 Content;
	real32 ContentHeight;
	real32 Spacing;
};

internal void BeginUI(ui_context *UI, render_commands *Cmd, mouse_input *Mouse, real32 DpiRatio)
{
	UI->Cmd = Cmd;
	UI->Mouse = Mouse;
	UI->DpiRatio = DpiRatio;

	if (!Mouse->Down && !Mouse->Released)
	{
		UI->ActiveID = 0;
	}
}

internal ui_style DefaultUIStyle()
{
	ui_style Style;

	Style.Panel = Vector4(0.025f, 0.14f, 0.16f, 0.92f);
	Style.WidgetColor = Vector4(0.08f, 0.22f, 0.24f, 0.95f);
	Style.HotColor = Vector4(0.18f, 0.36f, 0.38f, 0.95f);
	Style.ActiveColor = Vector4(0.82f, 0.72f, 0.59f, 1.0f);

	return Style;
}


internal rect2 UIScaleRect(ui_context *UI, rect2 Rect)
{
	return rect2(Rect.Min * UI->DpiRatio, Rect.Max * UI->DpiRatio);
}

internal rect2 UIIntersectRect(rect2 A, rect2 B)
{
	return rect2(Vector2(Maximum(A.Min.X, B.Min.X), Maximum(A.Min.Y, B.Min.Y)),
	             Vector2(Minimum(A.Max.X, B.Max.X), Minimum(A.Max.Y, B.Max.Y)));
}

internal bool32 UIRectHasArea(rect2 Rect)
{
	return Rect.Min.X < Rect.Max.X && Rect.Min.Y < Rect.Max.Y;
}

internal uint64 UIChildID(uint64 Parent, uint64 Key)
{
	uint64 ID = Parent ^ (Key + 0x9E3779B97F4A7C15ull + (Parent << 6) + (Parent >> 2));
	return ID ? ID : 1;
}

internal ui_element_state *UIGetScrollState(ui_context *UI, uint64 ID)
{
	Assert(ID != 0);

	for (uint32 Index = 0; Index < UI->ScrollStateCount; ++Index)
	{
		if (UI->ScrollStates[Index].ID == ID)
		{
			return &UI->ScrollStates[Index];
		}
	}

	if (UI->ScrollStateCount == ArrayCount(UI->ScrollStates))
	{
		Assert(!"UI scroll state pool is full");
		return &UI->OverflowState;
	}

	ui_element_state *State = &UI->ScrollStates[UI->ScrollStateCount++];
	ZeroStruct(*State);
	State->ID = ID;
	return State;
}

internal void UIRenderElement(ui_context *UI, ui_element Element)
{
	PushRenderRect(UI->Cmd, Element.Rect.Min, Element.Rect.Max, Element.Color);
}

internal void Panel(ui_context *UI, rect2 PanelRect)
{
	rect2 Rect = UIScaleRect(UI, PanelRect);
	if (PointInRect2(UI->Mouse->Position, Rect))
	{
		UI->Mouse->OverUI = true;
	}

	ui_element Element = {ui_element_type::Panel, Rect, UI->Style.Panel};
	UIRenderElement(UI, Element);
}

internal ui_scroll_list UIBeginScrollList(ui_context *UI, uint64 ID, rect2 PanelRect)
{
	ui_scroll_list List = {};
	List.UI = UI;
	List.ID = ID;
	List.State = UIGetScrollState(UI, ID);
	List.View = UIScaleRect(UI, PanelRect);

	real32 Padding = UI_SCROLL_PADDING * UI->DpiRatio;
	List.Content = rect2(List.View.Min + Vector2(Padding, Padding),
	                     List.View.Max - Vector2(Padding, Padding));
	List.Spacing = UI_ROW_SPACING * UI->DpiRatio;

	mouse_input *Mouse = UI->Mouse;
	if (PointInRect2(Mouse->Position, List.View))
	{
		Mouse->OverUI = true;
		List.State->Scroll -= (real32)Mouse->Wheel * UI_SCROLL_WHEEL_STEP * UI->DpiRatio;
	}

	real32 ViewHeight = Maximum(0.0f, List.Content.Max.Y - List.Content.Min.Y);
	real32 MaxScroll = Maximum(0.0f, List.State->ContentHeight - ViewHeight);
	List.State->Scroll = Clamp(0.0f, List.State->Scroll, MaxScroll);

	ui_element Element = {ui_element_type::Panel, List.View, UI->Style.Panel};
	UIRenderElement(UI, Element);
	return List;
}

internal bool32 UIButton(ui_scroll_list *List, uint64 Key, real32 Height, bool32 Selected = false)
{
	ui_context *UI = List->UI;
	mouse_input *Mouse = UI->Mouse;
	uint64 ID = UIChildID(List->ID, Key);

	real32 RowHeight = Height * UI->DpiRatio;
	real32 Y = List->Content.Min.Y + List->ContentHeight - List->State->Scroll;
	rect2 Rect = rect2(Vector2(List->Content.Min.X, Y),
	                   Vector2(List->Content.Max.X, Y + RowHeight));
	List->ContentHeight += RowHeight + List->Spacing;

	rect2 Visible = UIIntersectRect(Rect, List->Content);
	bool32 Hover = UIRectHasArea(Visible) && PointInRect2(Mouse->Position, Visible);
	bool32 Clicked = false;

	if (Hover)
	{
		Mouse->OverUI = true;
	}

	if (Hover && Mouse->Pressed && !Mouse->Consumed && !UI->ActiveID)
	{
		UI->ActiveID = ID;
	}

	bool32 Active = UI->ActiveID == ID;
	if (Active)
	{
		Mouse->Consumed = true;
		if (Mouse->Released)
		{
			Clicked = Hover;
			UI->ActiveID = 0;
		}
	}

	if (UIRectHasArea(Visible))
	{
		Vector4 Color = (Active || Selected) ? UI->Style.ActiveColor :
		                Hover ? UI->Style.HotColor : UI->Style.WidgetColor;
		ui_element Element = {ui_element_type::Button, Visible, Color};
		UIRenderElement(UI, Element);
	}

	return Clicked;
}

internal void UIEndScrollList(ui_scroll_list *List)
{
	real32 ContentHeight = List->ContentHeight;
	if (ContentHeight > 0.0f)
	{
		ContentHeight -= List->Spacing;
	}

	List->State->ContentHeight = ContentHeight;
	real32 ViewHeight = Maximum(0.0f, List->Content.Max.Y - List->Content.Min.Y);
	List->State->Scroll = Clamp(0.0f, List->State->Scroll,
	                            Maximum(0.0f, ContentHeight - ViewHeight));
}
