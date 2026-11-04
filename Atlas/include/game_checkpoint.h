#pragma once
#include "game_profile.h"
#include "doom_bag.h"
#include "turnhub_types.h"

namespace TurnHub {
// Serialization DTO only. GameEngine remains the sole live game state owner.
struct GameCheckpoint {
  // Match identity and roster labels survive reboot for history/export.
  char gameId[49]{};
  char names[MAX_PLAYERS][33]{};
  GameSettings settings{};
  DoomBag doom{};
  uint8_t count = 0, active = 0, starter = 0, winner = 0, monarch = 0, initiative = 0;
  bool paused = false, over = false;
  // Magic table state (schema 9): day/night, reversed turns and the players
  // (bit per index) whose rad trigger is waiting for an answer.
  DayNight dayNight = DayNight::Neither;
  bool reversed = false;
  uint16_t radDue = 0;
  // Every winner (bit playerNumber - 1); winner is its lowest bit. Several
  // only for a shared Star Commander victory.
  uint16_t winners = 0;
  // settings.turnTimerMs travels in schema 1's former per-turn "warningMs"
  // word: it was always 0 before the turn timer existed, which means OFF.
  uint32_t gameElapsed = 0, turnElapsed = 0, nextRequestId = 0;
  PlayerSeat players[MAX_PLAYERS]{};
  PlayerStats stats[MAX_PLAYERS]{};
  bool eliminated[MAX_PLAYERS]{};
  bool conceded[MAX_PLAYERS]{};
  bool selfEliminated[MAX_PLAYERS]{};
  bool eliminationPending[MAX_PLAYERS]{};
  uint8_t eliminatedBy[MAX_PLAYERS]{};
  PlayerCounters counters[MAX_PLAYERS]{};
  int32_t life[MAX_PLAYERS]{};
  int32_t damage[MAX_PLAYERS][MAX_PLAYERS][2]{};
};
bool validCheckpoint(const GameCheckpoint &saved);
// Clear the existing serialization workspace without constructing a second
// full Commander matrix on the caller's stack. Preserves all DTO defaults.
void clearCheckpoint(GameCheckpoint &saved);
} // namespace TurnHub
