#pragma once
#include "game_profile.h"
#include "storage.h"

namespace TurnHub {

TurnHubStorage::Status loadGameSettings(GameSettings &settings);
TurnHubStorage::Status saveGameSettings(const GameSettings &settings);

// "gamecfg" little-endian image. Schema 1: {1, profile, life[4]} (timer OFF).
// Schema 2 appends turnTimerMs[4]. Schema 3 adds loss flags (bit 4: house rules, bit 8: Planechase) and poisonLimit. Writes use schema 3. The profile
// byte's top bit is Two-Headed Giant (PROFILE_BYTE_TEAMS); bit 0x40 disables
// automatic zero-life elimination.
constexpr char GAME_SETTINGS_KEY[] = "gamecfg";
constexpr size_t GAME_SETTINGS_V1_SIZE = 6;
constexpr size_t GAME_SETTINGS_V2_SIZE = 10;
constexpr size_t GAME_SETTINGS_V3_SIZE = 15;

inline uint32_t readLe32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

inline void writeLe32(uint8_t *data, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) data[i] = static_cast<uint8_t>(value >> (8 * i));
}

inline TurnHubStorage::Status readGameSettings(TurnHubStorage::BlobStore &store, GameSettings &settings) {
  using TurnHubStorage::Status;
  const auto validSize = [](size_t size) {
    return size == GAME_SETTINGS_V1_SIZE || size == GAME_SETTINGS_V2_SIZE || size == GAME_SETTINGS_V3_SIZE;
  };
  size_t size = 0;
  Status status = store.read(GAME_SETTINGS_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (!validSize(size)) return Status::Corrupt;
  uint8_t data[GAME_SETTINGS_V3_SIZE] = {};
  status = store.read(GAME_SETTINGS_KEY, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (!validSize(size)) return Status::Corrupt;
  if (data[0] != 1 && data[0] != 2 && data[0] != 3) return Status::UnsupportedSchema;
  if (size != (data[0] == 1 ? GAME_SETTINGS_V1_SIZE : data[0] == 2 ? GAME_SETTINGS_V2_SIZE : GAME_SETTINGS_V3_SIZE)) return Status::Corrupt;

  GameSettings value;
  readProfileByte(data[1], value);
  const uint32_t life = readLe32(data + 2);
  if (life > static_cast<uint32_t>(LIFE_LIMIT)) return Status::Corrupt;
  value.startingLife = static_cast<int32_t>(life);
  value.turnTimerMs = data[0] >= 2 ? readLe32(data + 6) : TURN_TIMER_OFF;
  value.poisonLimit = value.twoHeadedGiant ? 15 : 10;
  if (data[0] == 3) {
    if (data[10] > 15) return Status::Corrupt;
    value.autoEliminateAtPoisonLimit = data[10] & 1;
    value.autoEliminateAtCommanderDamage = data[10] & 2;
    value.houseRules = data[10] & 4;
    value.planechase = data[10] & 8;
    value.poisonLimit = static_cast<int32_t>(readLe32(data + 11));
  }
  if (!validGameSettings(value)) return Status::Corrupt;
  // Saved before official rules (or edited by hand): official modes load official.
  applyOfficialRules(value);
  settings = value;
  return Status::Ok;
}

// Skips the write when the stored settings already match, sparing flash wear.
inline TurnHubStorage::Status writeGameSettings(TurnHubStorage::BlobStore &store, const GameSettings &settings) {
  using TurnHubStorage::Status;
  if (!validGameSettings(settings)) return Status::InvalidArgument;
  GameSettings old;
  const Status status = readGameSettings(store, old);
  if (status != Status::Ok && status != Status::NotFound) return status;
  if (status == Status::Ok && sameGameSettings(old, settings)) return Status::Ok;

  uint8_t data[GAME_SETTINGS_V3_SIZE] = {3, profileByte(settings)};
  writeLe32(data + 2, static_cast<uint32_t>(settings.startingLife));
  writeLe32(data + 6, settings.turnTimerMs);
  data[10] = (settings.autoEliminateAtPoisonLimit ? 1 : 0) | (settings.autoEliminateAtCommanderDamage ? 2 : 0) |
      (settings.houseRules ? 4 : 0) | (settings.planechase ? 8 : 0);
  writeLe32(data + 11, settings.poisonLimit);
  return store.write(GAME_SETTINGS_KEY, data, sizeof(data));
}

}  // namespace TurnHub
