#pragma once
#include <stdint.h>
#include <string.h>

namespace TurnHub {
enum class GameProfile : uint8_t { Generic, Magic, Commander, Yugioh, Count };
struct GameSettings {
  GameProfile profile = GameProfile::Generic;
  int32_t startingLife = 40;
};
inline bool validGameSettings(const GameSettings &settings) {
  return settings.profile < GameProfile::Count &&
      settings.startingLife >= 0 && settings.startingLife <= 1000000;
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
