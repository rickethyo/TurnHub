#pragma once
#include "game_checkpoint.h"
#include "storage.h"
#include <stddef.h>

namespace TurnHub {
class GameEngine;
class Lobby;
// The largest record: a 35-byte header and CRC-32, then per player 37 bytes
// plus 8 per opponent (Commander damage). 2,679 bytes for 16 players.
constexpr size_t GAME_CHECKPOINT_CAPACITY =
    35 + 4 + MAX_PLAYERS * (37 + MAX_PLAYERS * 8);
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
// never on the small ESP32 task stack; every record shares them, since each
// call finishes before the next. This is a persistence cache, not gameplay.
// The last saved record is remembered by size and CRC-32 rather than a second
// copy, to keep RAM free.
class GameRecovery {
 public:
  // key names this table's record in the store ("checkpoint" for Game 1).
  explicit GameRecovery(TurnHubStorage::BlobStore &store, const char *key = "checkpoint")
      : store_(store), key_(key) {}
  TurnHubStorage::Status load(GameEngine &game, Lobby &lobby, uint32_t nowMs);
  TurnHubStorage::Status save(const GameEngine &game, uint32_t nowMs);
  // Completion barrier: commit a finished snapshot before profile increments.
  // Never overwrites an unreadable/future record and never replays statistics.
  TurnHubStorage::Status saveCompleted(const GameEngine &game, uint32_t nowMs);
  TurnHubStorage::Status status() const { return status_; }
 private:
  void rememberSaved(uint32_t nowMs);

  TurnHubStorage::BlobStore &store_;
  const char *key_;
  static GameCheckpoint scratch_;
  static uint8_t bytes_[GAME_CHECKPOINT_CAPACITY];
  size_t previousSize_ = 0;
  uint32_t previousCrc_ = 0;
  uint32_t lastSavedMs_ = 0;
  bool writable_ = false;
  TurnHubStorage::Status status_ = TurnHubStorage::Status::Unavailable;
};
// Production NVS adapter, initialized explicitly during setup. Each game
// table has its own record (venue model): beginGameRecovery binds an engine
// to table slot's record, and the checkpoint calls find the record by engine.
constexpr uint8_t GAME_RECOVERY_SLOTS = 2;
TurnHubStorage::Status beginGameRecovery(GameEngine &game, Lobby &lobby, uint32_t nowMs, uint8_t slot = 0);
TurnHubStorage::Status checkpointGame(const GameEngine &game, uint32_t nowMs);
TurnHubStorage::Status checkpointCompletedGame(const GameEngine &game, uint32_t nowMs);
TurnHubStorage::Status gameRecoveryStatus(uint8_t slot = 0);
} // namespace TurnHub
