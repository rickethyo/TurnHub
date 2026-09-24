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

// Shared, greppable spelling for diagnostic logging. Hardware testing needs to
// tell these statuses apart on the serial console (NotFound vs Corrupt vs
// UnsupportedSchema vs IoError vs a clean Ok), so every recovery log line goes
// through this one helper instead of ad hoc strings.
inline const char *storageStatusName(TurnHubStorage::Status status) {
  using TurnHubStorage::Status;
  switch (status) {
    case Status::Ok: return "OK";
    case Status::NotFound: return "NOT_FOUND";
    case Status::Unavailable: return "UNAVAILABLE";
    case Status::InvalidArgument: return "INVALID_ARGUMENT";
    case Status::Corrupt: return "CORRUPT";
    case Status::UnsupportedSchema: return "UNSUPPORTED_SCHEMA";
    case Status::IoError: return "IO_ERROR";
    default: return "UNKNOWN";
  }
}

// Serialized on the Atlas application task. Buffers live with this service,
// never on the small ESP32 task stack. This is a persistence cache, not gameplay.
class GameRecovery {
 public:
  explicit GameRecovery(TurnHubStorage::BlobStore &store) : store_(store) {}
  TurnHubStorage::Status load(GameEngine &game, Lobby &lobby, uint32_t nowMs);
  TurnHubStorage::Status save(const GameEngine &game, uint32_t nowMs);
  TurnHubStorage::Status status() const { return status_; }
 private:
  void rememberSaved(uint32_t nowMs);

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
