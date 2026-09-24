#pragma once
#include "game_profile.h"
#include "storage.h"

namespace TurnHub {

TurnHubStorage::Status loadGameSettings(GameSettings &settings);
TurnHubStorage::Status saveGameSettings(const GameSettings &settings);

// "gamecfg" little-endian image. Schema 1: {1, profile, life[4]} (timer OFF).
// Schema 2 appends turnTimerMs[4]. Writes always use schema 2.
constexpr char GAME_SETTINGS_KEY[] = "gamecfg";
constexpr size_t GAME_SETTINGS_V1_SIZE = 6;
constexpr size_t GAME_SETTINGS_V2_SIZE = 10;

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
    return size == GAME_SETTINGS_V1_SIZE || size == GAME_SETTINGS_V2_SIZE;
  };
  size_t size = 0;
  Status status = store.read(GAME_SETTINGS_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (!validSize(size)) return Status::Corrupt;
  uint8_t data[GAME_SETTINGS_V2_SIZE] = {};
  status = store.read(GAME_SETTINGS_KEY, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (!validSize(size)) return Status::Corrupt;
  if (data[0] != 1 && data[0] != 2) return Status::UnsupportedSchema;
  if (size != (data[0] == 1 ? GAME_SETTINGS_V1_SIZE : GAME_SETTINGS_V2_SIZE)) return Status::Corrupt;

  GameSettings value;
  value.profile = static_cast<GameProfile>(data[1]);
  const uint32_t life = readLe32(data + 2);
  if (life > static_cast<uint32_t>(LIFE_LIMIT)) return Status::Corrupt;
  value.startingLife = static_cast<int32_t>(life);
  value.turnTimerMs = data[0] == 2 ? readLe32(data + 6) : TURN_TIMER_OFF;
  if (!validGameSettings(value)) return Status::Corrupt;
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

  uint8_t data[GAME_SETTINGS_V2_SIZE] = {2, static_cast<uint8_t>(settings.profile)};
  writeLe32(data + 2, static_cast<uint32_t>(settings.startingLife));
  writeLe32(data + 6, settings.turnTimerMs);
  return store.write(GAME_SETTINGS_KEY, data, sizeof(data));
}

}  // namespace TurnHub
