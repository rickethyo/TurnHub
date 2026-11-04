#pragma once

#include "game_engine.h"

namespace TurnHubProfileStats {

// The engine's game-completed callback: commits a finished recovery checkpoint
// before profile increments, attributed to the profiles captured at game start.
// Failed checkpoints skip statistics; interruption can leave partial results.
// This is not an exactly-once replay mechanism across multiple storage writes.
// profile_stats_bridge.cpp registers it at startup; host tests call it.
// A match shorter than shortGameMinimumMs (TurnHub::SHORT_GAME_MS; host
// scenarios set 0) commits its checkpoint but leaves statistics alone until
// the table counts it with countShortGame().
void persistCompletedGame(const TurnHub::GameEngine &game);
extern uint32_t shortGameMinimumMs;
// A finished match the completion callback held back as too short.
bool heldShortGame(const TurnHub::GameEngine &game);
// Records a held short match's statistics; returns the profiles updated.
uint8_t countShortGame(const TurnHub::GameEngine &game);

}  // namespace TurnHubProfileStats
