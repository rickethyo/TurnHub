#pragma once

#include "game_engine.h"

namespace TurnHubProfileStats {

// The engine's game-completed callback: commits a finished recovery checkpoint
// before profile increments, attributed to the profiles captured at game start.
// Failed checkpoints skip statistics; interruption can leave partial results.
// This is not an exactly-once replay mechanism across multiple storage writes.
// profile_stats_bridge.cpp registers it at startup; host tests call it.
void persistCompletedGame(const TurnHub::GameEngine &game);

}  // namespace TurnHubProfileStats
