#pragma once
#include <stdint.h>
#include <string.h>

// The game modes Atlas records in statistics and match history. The hub runs
// the games themselves (shared/core); Atlas only checks and files the results
// the hub imports (standalone_import.h).
namespace TurnHub {
enum class GameProfile : uint8_t { Generic, Magic, Commander, Yugioh, StarCommander, DoomCommander, Count };

// Bound on imported life totals and numbers. Life totals may go negative down
// to -LIFE_LIMIT.
constexpr int32_t LIFE_LIMIT = 99999;  // The app's MAX_LIFE (turnhub::Game::MAX_LIFE).

// Two-Headed Giant: Magic and Commander only (never Star Commander).
inline bool teamsAllowed(GameProfile profile) {
  return profile == GameProfile::Magic || profile == GameProfile::Commander;
}

inline const char *gameProfileKey(GameProfile profile) {
  switch (profile) {
    case GameProfile::Magic: return "mtg";
    case GameProfile::Commander: return "mtg_commander";
    case GameProfile::Yugioh: return "yugioh";
    case GameProfile::StarCommander: return "mtg_star_commander";
    case GameProfile::DoomCommander: return "mtg_bag_of_doom";
    default: return "generic";
  }
}
inline bool parseGameProfile(const char *key, GameProfile &profile) {
  for (uint8_t i=0; i<static_cast<uint8_t>(GameProfile::Count); ++i) {
    if (key && strcmp(key, gameProfileKey(static_cast<GameProfile>(i))) == 0) {
      profile = static_cast<GameProfile>(i); return true;
    }
  }
  return false;
}
} // namespace TurnHub
