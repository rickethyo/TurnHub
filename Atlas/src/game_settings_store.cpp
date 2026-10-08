#include "game_settings_store.h"
#include "nvs_blob_store.h"
#include "pairing_settings.h"
#include "setup_stage.h"
#include "speaker_settings.h"
#include "table_code_setting.h"
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
TurnHubStorage::Status loadPairingWindow(uint32_t &windowMs) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?readPairingWindow(store,windowMs):status;
}
TurnHubStorage::Status savePairingWindow(uint32_t windowMs) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?writePairingWindow(store,windowMs):status;
}
TurnHubStorage::Status loadSpeakerVolume(uint8_t &volume) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?readSpeakerVolume(store,volume):status;
}
TurnHubStorage::Status saveSpeakerVolume(uint8_t volume) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?writeSpeakerVolume(store,volume):status;
}
TurnHubStorage::Status loadTableCodeRequired(bool &required) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?readTableCodeRequired(store,required):status;
}
TurnHubStorage::Status saveTableCodeRequired(bool required) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?writeTableCodeRequired(store,required):status;
}
TurnHubStorage::Status loadSetupStage(SetupStage &stage) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?readSetupStage(store,stage):status;
}
TurnHubStorage::Status saveSetupStage(SetupStage stage) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?writeSetupStage(store,stage):status;
}
} // namespace TurnHub
