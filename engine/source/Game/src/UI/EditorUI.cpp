#pragma once

#include "Types.h"
#include "EngineMath.h"
#include "RenderCommands.h"
#include "GameState.h"

internal void DebugUI(game_state *GameState)
{
    ui_scroll_list List = UIBeginScrollList(&GameState->UI, 1,
                                           rect2(Vector2(20.0f, 20.0f), Vector2(240.0f, 140.0f)));

    for (uint32 Index = 1; Index < GameState->Storage.Count; ++Index)
    {
        if (GameState->Storage.LowEntities[Index].SimVariant.Type == Entity_Null)
        {
            continue;
        }

        if (UIButton(&List, Index, 22.0f, GameState->Gizmo.Selected == Index))
        {
            GameState->Gizmo.Selected = Index;
        }
    }

    UIEndScrollList(&List);
}
