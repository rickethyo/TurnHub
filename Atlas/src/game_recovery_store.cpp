#include "game_recovery.h"
#include "nvs_blob_store.h"
#include <Arduino.h>
#include "serial_log.h"

using TurnHub::serialLog;
namespace TurnHub {
namespace {
TurnHubStorage::NvsBlobStore store;
GameRecovery recovery(store);
TurnHubStorage::Status openStatus = TurnHubStorage::Status::Unavailable;
}
TurnHubStorage::Status beginGameRecovery(GameEngine &game, Lobby &lobby, uint32_t nowMs) {
  serialLog.println("ATLAS|RECOVERY|INIT");
  openStatus=store.begin("th_game_v1");
  serialLog.print("ATLAS|RECOVERY|OPEN|STATUS|"); serialLog.println(storageStatusName(openStatus));
  if (openStatus!=TurnHubStorage::Status::Ok) return openStatus;
  return recovery.load(game,lobby,nowMs);
}
TurnHubStorage::Status checkpointGame(const GameEngine &game, uint32_t nowMs) {
  return openStatus==TurnHubStorage::Status::Ok ? recovery.save(game,nowMs) : openStatus;
}
TurnHubStorage::Status gameRecoveryStatus() {
  return openStatus==TurnHubStorage::Status::Ok ? recovery.status() : openStatus;
}
} // namespace TurnHub
