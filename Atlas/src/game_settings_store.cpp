#include "game_settings_store.h"
#include "nvs_blob_store.h"
namespace TurnHub {
namespace { TurnHubStorage::NvsBlobStore store; }
TurnHubStorage::Status loadGameSettings(GameSettings &settings) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?readGameSettings(store,settings):status;
}
TurnHubStorage::Status saveGameSettings(const GameSettings &settings) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?writeGameSettings(store,settings):status;
}
} // namespace TurnHub
