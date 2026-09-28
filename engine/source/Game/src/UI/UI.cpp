#pragma once

#include "Types.h"
#include "Memory.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "Input.h"

#define SCROLL 10

struct Layout
{
	Vector2 Pivot;
};

enum ui_interaction_type
{
	UIInteraction_One = 0,
	UIInteraction_Hover,
	UIInteraction_Click,
	UIInteraction_ToggleBool,
	UIInteraction_DragReal32,
};

struct ui_interaction
{
	uint32 Type;
	uint32 Scroll;
	bool32 Hover;
	bool32 Click;
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


internal ui_interaction CheckUIInteraction(mouse_input *Mouse, rect2 PanelRect)
{
	ui_interaction interaction = {};

	interaction.Hover = PointInRect2(Mouse->Position, PanelRect);
	if (interaction.Hover)
	{
		interaction.Click = Mouse->Pressed;
		interaction.Scroll = Mouse->Wheel * SCROLL;
	}

	return interaction;
}

internal Layout StartLayout(ui_context* UI, Vector2 Pivot)
{
	Vector2 pivot = Vector2(Pivot.X * UI->DpiRatio, Pivot.Y * UI->DpiRatio);

	Layout layout;
	layout.Pivot = pivot;

	return layout;
}

internal void EndLayout()
{

}

internal void Panel(ui_context *UI, rect2 PanelRect)
{
	rect2 rect = rect2(PanelRect.Min * UI->DpiRatio, PanelRect.Max * UI->DpiRatio);
	Vector4 color = UI->Style.PanelColor;

	PushRenderRect(UI->Cmd, rect.Min, rect.Max, color);
}

internal void Button(ui_context* UI, rect2 ButtonRect)
{
	rect2 rect = rect2(ButtonRect.Min * UI->DpiRatio, ButtonRect.Max * UI->DpiRatio);
	Vector4 color = UI->Style.PanelColor;

	ui_interaction interaction = CheckUIInteraction(UI->Mouse, rect);

	if (interaction.Hover)
	{
		UI->Mouse->OverUI = true;
		color = UI->Style.HotColor;
	}

	if (interaction.Click)
	{
		UI->Mouse->OverUI = true;
		color = UI->Style.ActiveColor;
	}

	PushRenderRect(UI->Cmd, rect.Min, rect.Max, color);
}

internal void ScrollList(ui_context* UI, rect2 ScrollRect)
{
	rect2 rect = rect2(ScrollRect.Min * UI->DpiRatio, ScrollRect.Max * UI->DpiRatio);
	Vector4 color = UI->Style.PanelColor;

	ui_interaction interaction = CheckUIInteraction(UI->Mouse, rect);

	rect.Min.Y += interaction.Scroll;
	rect.Max.Y += interaction.Scroll;

	PushRenderRect(UI->Cmd, rect.Min, rect.Max, color);
}

internal void Image(ui_context* UI, rect2 PanelRect)
{
	rect2 rect = rect2(PanelRect.Min * UI->DpiRatio, PanelRect.Max * UI->DpiRatio);
}
