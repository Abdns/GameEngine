#pragma once

#include "Types.h"
#include "Memory.h"
#include "EngineMath.h"
#include "PlatformAPI.h"
#include "RenderCommands.h"
#include "Input.h"
#include "Text.cpp"


struct ui_style
{
	Vector4 Panel;
	Vector4 WidgetColor;
	Vector4 HotColor;
	Vector4 TextColor;
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

	Style.Panel = Vector4(0.025f, 0.14f, 0.16f, 0.92f);
	Style.WidgetColor = Vector4(0.08f, 0.22f, 0.24f, 0.95f);
	Style.HotColor = Vector4(0.18f, 0.36f, 0.38f, 0.95f);
	Style.TextColor = Vector4(0.82f, 0.72f, 0.59f, 1.0f);

	return Style;
}


internal bool32 CheckUIInteraction(mouse_input *Mouse, rect2 PanelRect)
{
	return PointInRect2(Mouse->Position, PanelRect);
}

internal void Panel(ui_context *UI, rect2 PanelRect)
{
	rect2 rect = rect2(PanelRect.Min * UI->DpiRatio, PanelRect.Max * UI->DpiRatio);

	Vector4 color = UI->Style.Panel;

	if (CheckUIInteraction(UI->Mouse, rect))
	{
		UI->Mouse->OverUI = true;
		color = UI->Style.HotColor;
	}

	PushRenderRect(UI->Cmd, rect.Min, rect.Max, color);
}
