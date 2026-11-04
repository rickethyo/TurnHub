// NVS records for Atlas device settings (the turnhub namespace). Host tests
// stub these functions in profile_fixture.cpp.
#include "nvs_blob_store.h"
#include "pairing_settings.h"
#include "power_settings.h"
#include "setup_stage.h"
#include "speaker_settings.h"
#include "table_code_setting.h"
#include "upload_approval.h"
namespace TurnHub {
namespace { TurnHubStorage::NvsBlobStore store; }
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
TurnHubStorage::Status loadPowerSettings(PowerSettings &settings) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?readPowerSettings(store,settings):status;
}
TurnHubStorage::Status savePowerSettings(const PowerSettings &settings) {
  const auto status=store.begin("turnhub");
  return status==TurnHubStorage::Status::Ok?writePowerSettings(store,settings):status;
}
TurnHubStorage::Status loadUploadApprovalRequired(bool &required) {
  const auto status = store.begin("turnhub");
  return status == TurnHubStorage::Status::Ok ? readUploadApprovalRequired(store, required) : status;
}
TurnHubStorage::Status saveUploadApprovalRequired(bool required) {
  const auto status = store.begin("turnhub");
  return status == TurnHubStorage::Status::Ok ? writeUploadApprovalRequired(store, required) : status;
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
