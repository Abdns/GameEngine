#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"
#include "GameState.h"

internal void DebugUI(game_state *GameState)
{
	ui_context *UI = &GameState->UI;

	rect2 ListRect = rect2(Vector2(20.0f, 20.0f), Vector2(280.0f, 152.0f));

	entity_storage *Storage = &GameState->Storage;

	real32 RowHeight = 24.0f;

	Layout List = ScrollList(UI, CreateLayout(UI, ListRect.Min), ListRect);

	for (uint32 Index = 1; Index < Storage->Count; ++Index)
	{
		if (Storage->LowEntities[Index].SimVariant.Type == Entity_Null)
		{
			continue;
		}

		rect2 Row = LayoutRow(&List, RowHeight);

		bool32 Selected = (GameState->Gizmo.Selected == Index);
		if (Button(UI, List, Row, Selected))
		{
			GameState->Gizmo.Selected = Selected ? ENTITY_STORAGE_NONE : Index;
		}
	}
}
