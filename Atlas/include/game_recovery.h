#pragma once
#include "game_checkpoint.h"
#include "storage.h"
#include <stddef.h>

namespace TurnHub {
class GameEngine;
class Lobby;
constexpr size_t GAME_CHECKPOINT_CAPACITY = 4096;
size_t encodeCheckpoint(const GameCheckpoint &saved, uint8_t *bytes, size_t capacity);
TurnHubStorage::Status decodeCheckpoint(const uint8_t *bytes, size_t size, GameCheckpoint &saved);

// Serialized on the Atlas application task. Buffers live with this service,
// never on the small ESP32 task stack. This is a persistence cache, not gameplay.
class GameRecovery {
 public:
  explicit GameRecovery(TurnHubStorage::BlobStore &store) : store_(store) {}
  TurnHubStorage::Status load(GameEngine &game, Lobby &lobby, uint32_t nowMs);
  TurnHubStorage::Status save(const GameEngine &game, uint32_t nowMs);
  TurnHubStorage::Status status() const { return status_; }
 private:
  TurnHubStorage::BlobStore &store_;
  GameCheckpoint scratch_{};
  uint8_t bytes_[GAME_CHECKPOINT_CAPACITY]{};
  uint8_t previous_[GAME_CHECKPOINT_CAPACITY]{};
  size_t previousSize_ = 0;
  uint32_t lastSavedMs_ = 0;
  bool writable_ = false;
  TurnHubStorage::Status status_ = TurnHubStorage::Status::Unavailable;
};
// Production NVS adapter, initialized explicitly during setup.
TurnHubStorage::Status beginGameRecovery(GameEngine &game, Lobby &lobby, uint32_t nowMs);
TurnHubStorage::Status checkpointGame(const GameEngine &game, uint32_t nowMs);
TurnHubStorage::Status gameRecoveryStatus();
} // namespace TurnHub
