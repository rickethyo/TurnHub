#pragma once
#include "game_profile.h"
#include "turnhub_types.h"

namespace TurnHub {
// Serialization DTO only. GameEngine remains the sole live game state owner.
struct GameCheckpoint {
  GameSettings settings{};
  uint8_t count = 0, active = 0, starter = 0, winner = 0;
  bool paused = false, over = false;
  // settings.turnTimerMs travels in schema 1's former per-turn "warningMs"
  // word: it was always 0 before the turn timer existed, which means OFF.
  uint32_t gameElapsed = 0, turnElapsed = 0, nextRequestId = 0;
  PlayerSeat players[MAX_PLAYERS]{};
  PlayerStats stats[MAX_PLAYERS]{};
  bool eliminated[MAX_PLAYERS]{};
  int32_t life[MAX_PLAYERS]{};
  int32_t damage[MAX_PLAYERS][MAX_PLAYERS][2]{};
};
bool validCheckpoint(const GameCheckpoint &saved);
} // namespace TurnHub
