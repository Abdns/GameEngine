#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"
#include "GameState.h"

internal void DebugUI(game_state *GameState)
{
	rect2 panelRect = rect2(Vector2(0.0f, 0.0f), Vector2(100.0f, 100.0f));
	Panel(&GameState->UI, panelRect);
}