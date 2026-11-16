#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"
#include <stdio.h>
// Included through DebugEditor.cpp after game_state is defined in Game.cpp.

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

		const char *Name = LowEntityName(Storage, Index);
		char DefaultName[32];
		if (!Name[0])
		{
			snprintf(DefaultName, sizeof(DefaultName), "Entity %u", Index);
			Name = DefaultName;
		}
		bool32 Selected = (GameState->Gizmo.Selected == Index);
		if (Button(UI, List, Row, Selected, Name))
		{
			GameState->Gizmo.Selected = Selected ? ENTITY_STORAGE_NONE : Index;
		}
	}
}
