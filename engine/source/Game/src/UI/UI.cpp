#pragma once

#include "Types.h"
#include "Memory.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "Input.h"

#define UI_WHEEL_DELTA 120
#define UI_WHEEL_STEP  24.0f

struct Layout
{
	Vector2 Pivot;
	real32  Width;
	real32  DpiRatio;
	rect2   Clip;
	bool32  Clipped;
};

enum ui_interaction_type
{
	UIInteraction_None = 0,
	UIInteraction_Hover,
	UIInteraction_Click,
	UIInteraction_ToggleBool,
	UIInteraction_DragReal32,
};

struct ui_interaction
{
	uint32 Type;
	real32 Scroll;
};

struct ui_style
{
	Vector4 PanelColor;
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

	real32 Scroll;
};

internal void BeginUI(ui_context *UI, render_commands *Cmd, mouse_input *Mouse, real32 DpiRatio)
{
	UI->Cmd = Cmd;
	UI->Mouse = Mouse;
	UI->DpiRatio = DpiRatio;
}

internal ui_style DefaultUIStyle()
{
	ui_style Style;

	Style.PanelColor = Vector4(0.025f, 0.14f, 0.16f, 0.92f);
	Style.WidgetColor = Vector4(0.08f, 0.22f, 0.24f, 0.95f);
	Style.HotColor = Vector4(0.18f, 0.36f, 0.38f, 0.95f);
	Style.ActiveColor = Vector4(0.82f, 0.72f, 0.59f, 1.0f);

	return Style;
}

internal rect2 DpiScaleRect(ui_context *UI, rect2 Rect)
{
	rect2 result = rect2(Rect.Min * UI->DpiRatio, Rect.Max * UI->DpiRatio);

	return result;
}

internal ui_interaction CheckUIInteraction(ui_context *UI, rect2 Rect)
{
	ui_interaction interaction = {};

	if (PointInRect2(UI->Mouse->Position, Rect))
	{
		interaction.Type = UIInteraction_Hover;
		if (UI->Mouse->Pressed)
		{
			interaction.Type = UIInteraction_Click;
		}
		interaction.Scroll = (real32)UI->Mouse->Wheel / (real32)UI_WHEEL_DELTA;
	}

	return interaction;
}

internal Layout CreateLayout(ui_context *UI, Vector2 Pivot)
{
	Layout layout = {};

	layout.Pivot    = Vector2(Pivot.X * UI->DpiRatio, Pivot.Y * UI->DpiRatio);
	layout.DpiRatio = UI->DpiRatio;

	return layout;
}

internal rect2 LayoutRow(Layout *layout, real32 Height)
{
	rect2 rect = rect2(layout->Pivot, Vector2(layout->Pivot.X + layout->Width, layout->Pivot.Y + Height * layout->DpiRatio));

	layout->Pivot.Y = rect.Max.Y;

	return rect;
}

internal rect2 ClipRect(rect2 Rect, rect2 Clip)
{
	Rect.Min.X = Maximum(Rect.Min.X, Clip.Min.X);
	Rect.Min.Y = Maximum(Rect.Min.Y, Clip.Min.Y);
	Rect.Max.X = Minimum(Rect.Max.X, Clip.Max.X);
	Rect.Max.Y = Minimum(Rect.Max.Y, Clip.Max.Y);

	return Rect;
}

internal void Panel(ui_context *UI, rect2 PanelRect)
{
	rect2 rect = DpiScaleRect(UI, PanelRect);

	PushRenderRect(UI->Cmd, rect.Min, rect.Max, UI->Style.PanelColor);
}

internal bool32 Button(ui_context *UI, Layout layout, rect2 ButtonRect, bool32 Selected)
{
	if (layout.Clipped)
	{
		ButtonRect = ClipRect(ButtonRect, layout.Clip);

		if (ButtonRect.Max.X <= ButtonRect.Min.X || ButtonRect.Max.Y <= ButtonRect.Min.Y)
		{
			return false;
		}
	}

	rect2 rect = ButtonRect;

	ui_interaction interaction = CheckUIInteraction(UI, ButtonRect);

	Vector4 color = Selected ? UI->Style.ActiveColor : UI->Style.WidgetColor;
	bool32 clicked = false;

	if (interaction.Type != UIInteraction_None)
	{
		UI->Mouse->OverUI = true;
	}

	if (interaction.Type == UIInteraction_Hover)
	{
		color = UI->Style.HotColor;
	}

	if (interaction.Type == UIInteraction_Click)
	{
		color   = UI->Style.ActiveColor;
		clicked = true;
	}

	PushRenderRect(UI->Cmd, rect.Min, rect.Max, color);

	return clicked;
}

internal Layout ScrollList(ui_context *UI, Layout layout, rect2 ScrollRect)
{
	rect2 rect = DpiScaleRect(UI, ScrollRect);

	ui_interaction interaction = CheckUIInteraction(UI, rect);
	if (interaction.Type != UIInteraction_None)
	{
		UI->Mouse->OverUI = true;
	}

	UI->Scroll -= interaction.Scroll * UI_WHEEL_STEP * UI->DpiRatio;
	UI->Scroll  = Maximum(0.0f, UI->Scroll);

	layout.Pivot   = Vector2(rect.Min.X, rect.Min.Y - UI->Scroll);
	layout.Width   = rect.Max.X - rect.Min.X;
	layout.Clip    = rect;
	layout.Clipped = true;

	PushRenderRect(UI->Cmd, rect.Min, rect.Max, UI->Style.PanelColor);

	return layout;
}

internal void Image(ui_context *UI, rect2 PanelRect)
{
	rect2 rect = DpiScaleRect(UI, PanelRect);
}
