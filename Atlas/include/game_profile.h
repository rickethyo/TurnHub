#pragma once
#include <stdint.h>
#include <string.h>

namespace TurnHub {
enum class GameProfile : uint8_t { Generic, Magic, Commander, Yugioh, Count };

// Bound on starting life, life totals, life deltas and Commander damage.
// Life totals may go negative down to -LIFE_LIMIT.
constexpr int32_t LIFE_LIMIT = 1000000;

// Turn timer. 0 means OFF: no countdown, only the gentle long-turn cue after
// TURN_TIMER_LONG_TURN_MS. Any other value is a per-turn countdown in whole
// seconds. Presets are UI shortcuts only; the wire/storage value is always the
// duration, so a custom value and a preset of the same length are identical.
constexpr uint32_t TURN_TIMER_OFF = 0;
constexpr uint32_t TURN_TIMER_MIN_MS = 15000;
constexpr uint32_t TURN_TIMER_MAX_MS = 3600000;
constexpr uint32_t TURN_TIMER_WARNING_MS = 10000;   // Remaining time that starts the warning cue.
constexpr uint32_t TURN_TIMER_LONG_TURN_MS = 300000; // OFF mode: "green at 5 minutes".
constexpr uint32_t TURN_TIMER_PRESETS_MS[] = {TURN_TIMER_OFF, 60000, 120000, 180000, 300000};
constexpr uint8_t TURN_TIMER_PRESET_COUNT = sizeof(TURN_TIMER_PRESETS_MS) / sizeof(TURN_TIMER_PRESETS_MS[0]);

inline bool validTurnTimerMs(uint32_t ms) {
  return ms == TURN_TIMER_OFF ||
      (ms >= TURN_TIMER_MIN_MS && ms <= TURN_TIMER_MAX_MS && ms % 1000 == 0);
}

// Two-Headed Giant (Magic and Commander only): teams of two neighbours in
// turn order (seats 1+2, 3+4, ...) share one life total and one turn. Needs an
// even table of at least TEAM_MIN_PLAYERS. Default team life is 30 (60 for
// Commander); the starting life setting is the team's total.
constexpr uint8_t TEAM_SIZE = 2;
constexpr uint8_t TEAM_MIN_PLAYERS = 4;
constexpr int32_t TEAM_LIFE_MAGIC = 30;
constexpr int32_t TEAM_LIFE_COMMANDER = 60;
// Saved and recovered settings carry the team flag in the profile byte's top
// bit, so the stored layouts did not change.
constexpr uint8_t PROFILE_BYTE_TEAMS = 0x80;
// The ConfigureGame Intent's payload.flags bit for the same setting.
constexpr uint32_t GAME_FLAG_TWO_HEADED_GIANT = 0x100;

struct GameSettings {
  GameProfile profile = GameProfile::Generic;
  int32_t startingLife = 40;
  uint32_t turnTimerMs = TURN_TIMER_OFF;
  bool twoHeadedGiant = false;
};
inline bool teamsAllowed(GameProfile profile) {
  return profile == GameProfile::Magic || profile == GameProfile::Commander;
}
inline bool validGameSettings(const GameSettings &settings) {
  return settings.profile < GameProfile::Count &&
      settings.startingLife >= 0 && settings.startingLife <= LIFE_LIMIT &&
      validTurnTimerMs(settings.turnTimerMs) &&
      (!settings.twoHeadedGiant || teamsAllowed(settings.profile));
}
inline bool sameGameSettings(const GameSettings &a, const GameSettings &b) {
  return a.profile == b.profile && a.startingLife == b.startingLife && a.turnTimerMs == b.turnTimerMs &&
      a.twoHeadedGiant == b.twoHeadedGiant;
}
inline bool validTeamTable(const GameSettings &settings, uint8_t playerCount) {
  return !settings.twoHeadedGiant ||
      (playerCount >= TEAM_MIN_PLAYERS && playerCount % TEAM_SIZE == 0);
}
inline uint8_t profileByte(const GameSettings &settings) {
  return static_cast<uint8_t>(settings.profile) | (settings.twoHeadedGiant ? PROFILE_BYTE_TEAMS : 0);
}
inline void readProfileByte(uint8_t value, GameSettings &settings) {
  settings.profile = static_cast<GameProfile>(value & static_cast<uint8_t>(~PROFILE_BYTE_TEAMS));
  settings.twoHeadedGiant = (value & PROFILE_BYTE_TEAMS) != 0;
}
inline const char *gameProfileKey(GameProfile profile) {
  switch (profile) {
    case GameProfile::Magic: return "mtg";
    case GameProfile::Commander: return "mtg_commander";
    case GameProfile::Yugioh: return "yugioh";
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
