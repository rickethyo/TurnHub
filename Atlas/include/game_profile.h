#pragma once
#include <stdint.h>
#include <string.h>

namespace TurnHub {
enum class GameProfile : uint8_t { Generic, Magic, Commander, Yugioh, StarCommander, DoomCommander, Count };

// Commander rules (Commander damage, commander casts and partners): Commander
// and its Star Commander / Bag of Doom variants.
inline bool commanderRules(GameProfile profile) {
  return profile == GameProfile::Commander || profile == GameProfile::StarCommander || profile == GameProfile::DoomCommander;
}
// Magic rules (monarch, initiative, poison, energy, experience, city's
// blessing): Magic and every Commander variant.
inline bool magicRules(GameProfile profile) {
  return profile == GameProfile::Magic || commanderRules(profile);
}

// Star Commander: exactly five players. Relationships follow the seat (player
// number 1..5, fixed for the match, also after elimination): the two
// neighbours are allies and the two across the table are enemies. A player
// wins when both enemies are eliminated; allies cannot deal each other
// Commander damage. Never tied to names, profiles or Sigil hardware.
constexpr uint8_t STAR_PLAYERS = 5;
constexpr int32_t STAR_DEFAULT_LIFE = 40;
inline bool validStarPlayer(uint8_t playerNumber) {
  return playerNumber >= 1 && playerNumber <= STAR_PLAYERS;
}
// Writes the two allies (player numbers) of playerNumber; false when invalid.
inline bool starAllies(uint8_t playerNumber, uint8_t out[2]) {
  if (!validStarPlayer(playerNumber)) return false;
  const uint8_t i = static_cast<uint8_t>(playerNumber - 1);
  out[0] = static_cast<uint8_t>((i + 1) % STAR_PLAYERS + 1);
  out[1] = static_cast<uint8_t>((i + 4) % STAR_PLAYERS + 1);
  return true;
}
// Writes the two enemies (player numbers) of playerNumber; false when invalid.
inline bool starEnemies(uint8_t playerNumber, uint8_t out[2]) {
  if (!validStarPlayer(playerNumber)) return false;
  const uint8_t i = static_cast<uint8_t>(playerNumber - 1);
  out[0] = static_cast<uint8_t>((i + 2) % STAR_PLAYERS + 1);
  out[1] = static_cast<uint8_t>((i + 3) % STAR_PLAYERS + 1);
  return true;
}
// Distinct seated players who are allies.
inline bool starAllied(uint8_t a, uint8_t b) {
  uint8_t allies[2] = {};
  return starAllies(a, allies) && (allies[0] == b || allies[1] == b);
}
inline bool starEnemy(uint8_t a, uint8_t b) {
  uint8_t enemies[2] = {};
  return starEnemies(a, enemies) && (enemies[0] == b || enemies[1] == b);
}

// Parity with the standalone app (StandaloneGame.UNDO_START_MS, SHORT_GAME_MS):
// Back to setup undoes a start within the first three minutes of game time,
// and a match that ends within a minute waits for the table to count it.
constexpr uint32_t UNDO_START_MS = 3 * 60000;
constexpr uint32_t SHORT_GAME_MS = 60000;

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
constexpr uint32_t GAME_FLAG_MANUAL_ELIMINATION = 0x200;
constexpr uint8_t PROFILE_BYTE_MANUAL_ELIMINATION = 0x40;

constexpr uint32_t GAME_FLAG_MANUAL_POISON = 0x400;
constexpr uint32_t GAME_FLAG_MANUAL_COMMANDER = 0x800;
constexpr uint32_t GAME_FLAG_HOUSE_RULES = 0x1000;
constexpr int32_t COMMANDER_DAMAGE_LIMIT = 21;
enum class CounterKind : uint8_t { CommanderDamage, Poison, Energy, Experience, CommanderCast, CityBlessing, Initiative, Count };
struct PlayerCounters {
  int32_t poison = 0, energy = 0, experience = 0;
  int32_t commanderCasts[2] = {};
  bool cityBlessing = false;
};
inline bool sameCounters(const PlayerCounters &a, const PlayerCounters &b) {
  return a.poison == b.poison && a.energy == b.energy && a.experience == b.experience &&
      a.commanderCasts[0] == b.commanderCasts[0] && a.commanderCasts[1] == b.commanderCasts[1] && a.cityBlessing == b.cityBlessing;
}
inline bool validCounters(const PlayerCounters &c) {
  return c.poison >= 0 && c.poison <= LIFE_LIMIT && c.energy >= 0 && c.energy <= LIFE_LIMIT &&
      c.experience >= 0 && c.experience <= LIFE_LIMIT && c.commanderCasts[0] >= 0 && c.commanderCasts[0] <= LIFE_LIMIT &&
      c.commanderCasts[1] >= 0 && c.commanderCasts[1] <= LIFE_LIMIT;
}

struct GameSettings {
  GameProfile profile = GameProfile::Generic;
  int32_t startingLife = 40;
  uint32_t turnTimerMs = TURN_TIMER_OFF;
  bool twoHeadedGiant = false;
  bool autoEliminateAtZeroLife = true;
  bool autoEliminateAtPoisonLimit = true;
  bool autoEliminateAtCommanderDamage = true;
  int32_t poisonLimit = 10;
  // Official modes (officialRules) keep their official starting life, poison
  // limit and player count unless the table turns house rules on.
  bool houseRules = false;
};
// Two-Headed Giant: Magic and Commander only (never Star Commander).
inline bool teamsAllowed(GameProfile profile) {
  return profile == GameProfile::Magic || profile == GameProfile::Commander;
}

// An officially published mode's fixed setup (Magic: the Comprehensive Rules
// of 2026-09-25; the app's OfficialRules.kt carries the rule numbers). Custom
// modes (Generic, Star Commander, Two-Headed Giant Commander) have none.
// A plain aggregate (no member initializers) so it brace-initializes under the
// firmware's C++11.
struct OfficialRules {
  int32_t life;
  int32_t poisonLimit;   // 0: the game has no poison (Yu-Gi-Oh!).
  uint8_t players;       // Exactly this many; 0 for any count.
};
inline bool officialRules(GameProfile profile, bool teams, OfficialRules &out) {
  if (profile == GameProfile::Magic && teams) { out = OfficialRules{TEAM_LIFE_MAGIC, 15, 4}; return true; }
  if (profile == GameProfile::Magic) { out = OfficialRules{20, 10, 0}; return true; }
  if (profile == GameProfile::Commander && !teams) { out = OfficialRules{40, 10, 0}; return true; }
  if (profile == GameProfile::Yugioh) { out = OfficialRules{8000, 0, 2}; return true; }
  return false;
}
// The official rules these settings follow; false for a custom mode or house rules.
inline bool lockedRules(const GameSettings &settings, OfficialRules &out) {
  return !settings.houseRules && officialRules(settings.profile, settings.twoHeadedGiant, out);
}
// Puts an official mode's starting life and poison limit back, unless house
// rules are on. Returns true when anything changed.
inline bool applyOfficialRules(GameSettings &settings) {
  OfficialRules rules{};
  if (!lockedRules(settings, rules)) return false;
  const bool changed = settings.startingLife != rules.life ||
      (rules.poisonLimit && settings.poisonLimit != rules.poisonLimit);
  settings.startingLife = rules.life;
  if (rules.poisonLimit) settings.poisonLimit = rules.poisonLimit;
  return changed;
}
// The settings keep an official mode's values (always true with house rules).
inline bool followsOfficialRules(const GameSettings &settings) {
  OfficialRules rules{};
  return !lockedRules(settings, rules) || (settings.startingLife == rules.life &&
      (!rules.poisonLimit || settings.poisonLimit == rules.poisonLimit));
}
// An official mode's exact player count (two teams of two, a two-player Duel).
inline bool validOfficialTable(const GameSettings &settings, uint8_t playerCount) {
  OfficialRules rules{};
  return !lockedRules(settings, rules) || !rules.players || playerCount == rules.players;
}
inline bool validGameSettings(const GameSettings &settings) {
  return settings.profile < GameProfile::Count &&
      settings.startingLife >= 0 && settings.startingLife <= LIFE_LIMIT &&
      validTurnTimerMs(settings.turnTimerMs) && settings.poisonLimit >= 1 && settings.poisonLimit <= LIFE_LIMIT &&
      (!settings.twoHeadedGiant || teamsAllowed(settings.profile));
}
inline bool sameGameSettings(const GameSettings &a, const GameSettings &b) {
  return a.profile == b.profile && a.startingLife == b.startingLife && a.turnTimerMs == b.turnTimerMs &&
      a.twoHeadedGiant == b.twoHeadedGiant && a.houseRules == b.houseRules &&
      a.autoEliminateAtZeroLife == b.autoEliminateAtZeroLife &&
      a.autoEliminateAtPoisonLimit == b.autoEliminateAtPoisonLimit &&
      a.autoEliminateAtCommanderDamage == b.autoEliminateAtCommanderDamage && a.poisonLimit == b.poisonLimit;
}
inline bool validTeamTable(const GameSettings &settings, uint8_t playerCount) {
  return !settings.twoHeadedGiant ||
      (playerCount >= TEAM_MIN_PLAYERS && playerCount % TEAM_SIZE == 0);
}
inline bool validStarTable(const GameSettings &settings, uint8_t playerCount) {
  return (settings.profile != GameProfile::StarCommander || playerCount == STAR_PLAYERS) &&
      (settings.profile != GameProfile::DoomCommander || playerCount >= 5);
}
inline uint8_t profileByte(const GameSettings &settings) {
  return static_cast<uint8_t>(settings.profile) | (settings.twoHeadedGiant ? PROFILE_BYTE_TEAMS : 0) |
      (settings.autoEliminateAtZeroLife ? 0 : PROFILE_BYTE_MANUAL_ELIMINATION);
}
inline void readProfileByte(uint8_t value, GameSettings &settings) {
  settings.profile = static_cast<GameProfile>(value & static_cast<uint8_t>(~(PROFILE_BYTE_TEAMS | PROFILE_BYTE_MANUAL_ELIMINATION)));
  settings.twoHeadedGiant = (value & PROFILE_BYTE_TEAMS) != 0;
  settings.autoEliminateAtZeroLife = (value & PROFILE_BYTE_MANUAL_ELIMINATION) == 0;
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
