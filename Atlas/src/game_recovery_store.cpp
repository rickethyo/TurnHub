#include "game_recovery.h"
#include "nvs_blob_store.h"
namespace TurnHub {
namespace {
TurnHubStorage::NvsBlobStore store;
GameRecovery recovery(store);
TurnHubStorage::Status openStatus = TurnHubStorage::Status::Unavailable;
}
TurnHubStorage::Status beginGameRecovery(GameEngine &game, Lobby &lobby, uint32_t nowMs) {
  openStatus=store.begin("th_game_v1");
  return openStatus==TurnHubStorage::Status::Ok ? recovery.load(game,lobby,nowMs) : openStatus;
}
TurnHubStorage::Status checkpointGame(const GameEngine &game, uint32_t nowMs) {
  return openStatus==TurnHubStorage::Status::Ok ? recovery.save(game,nowMs) : openStatus;
}
TurnHubStorage::Status gameRecoveryStatus() {
  return openStatus==TurnHubStorage::Status::Ok ? recovery.status() : openStatus;
}
} // namespace TurnHub
