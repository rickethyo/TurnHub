#pragma once

#include "game_engine.h"

namespace TurnHubProfileStats {

// The engine's game-completed callback: commits each finished game's
// statistics once, attributed to the profiles captured at game start.
// profile_stats_bridge.cpp registers it at startup; host tests call it.
void persistCompletedGame(const TurnHub::GameEngine &game);

}  // namespace TurnHubProfileStats
