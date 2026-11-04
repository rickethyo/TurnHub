#include "game_recovery.h"
#include "nvs_blob_store.h"
#include <Arduino.h>
#include "serial_log.h"

using TurnHub::serialLog;
namespace TurnHub {
namespace {
TurnHubStorage::NvsBlobStore store;
GameRecovery recoveries[GAME_RECOVERY_SLOTS] = {
    GameRecovery(store, "checkpoint"), GameRecovery(store, "checkpoint2")};
const GameEngine *engines[GAME_RECOVERY_SLOTS] = {};
TurnHubStorage::Status openStatus = TurnHubStorage::Status::Unavailable;

// The engine's record; an engine no boot bound yet takes a free one.
GameRecovery *recoveryFor(const GameEngine &game) {
  for (uint8_t i = 0; i < GAME_RECOVERY_SLOTS; ++i) {
    if (engines[i] == &game) return &recoveries[i];
  }
  for (uint8_t i = 0; i < GAME_RECOVERY_SLOTS; ++i) {
    if (engines[i] == nullptr) {
      engines[i] = &game;
      return &recoveries[i];
    }
  }
  return nullptr;
}
}
TurnHubStorage::Status beginGameRecovery(GameEngine &game, Lobby &lobby, uint32_t nowMs, uint8_t slot) {
  if (slot >= GAME_RECOVERY_SLOTS) return TurnHubStorage::Status::InvalidArgument;
  serialLog.print("ATLAS|RECOVERY|INIT|GAME|"); serialLog.println(slot + 1);
  engines[slot] = &game;
  openStatus=store.begin("th_game_v1");
  serialLog.print("ATLAS|RECOVERY|OPEN|STATUS|"); serialLog.println(storageStatusName(openStatus));
  if (openStatus!=TurnHubStorage::Status::Ok) return openStatus;
  return recoveries[slot].load(game,lobby,nowMs);
}
TurnHubStorage::Status checkpointGame(const GameEngine &game, uint32_t nowMs) {
  GameRecovery *recovery = recoveryFor(game);
  if (recovery == nullptr) return TurnHubStorage::Status::Unavailable;
  return openStatus==TurnHubStorage::Status::Ok ? recovery->save(game,nowMs) : openStatus;
}
TurnHubStorage::Status checkpointCompletedGame(const GameEngine &game, uint32_t nowMs) {
  GameRecovery *recovery = recoveryFor(game);
  if (recovery == nullptr) return TurnHubStorage::Status::Unavailable;
  // Opening is idempotent. If boot could not open NVS, a completion still has
  // to obtain a successful commit before statistics may change.
  openStatus=store.begin("th_game_v1");
  return openStatus==TurnHubStorage::Status::Ok ? recovery->saveCompleted(game,nowMs) : openStatus;
}
TurnHubStorage::Status gameRecoveryStatus(uint8_t slot) {
  if (slot >= GAME_RECOVERY_SLOTS) return TurnHubStorage::Status::InvalidArgument;
  return openStatus==TurnHubStorage::Status::Ok ? recoveries[slot].status() : openStatus;
}
} // namespace TurnHub
