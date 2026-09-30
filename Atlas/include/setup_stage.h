#pragma once

// First-run guided setup: how far this Atlas is through it. Atlas owns the
// stage; the Android app and the portal follow the same steps and read it
// through GET /api/setup. See Documentation/engineering/FIRST_RUN_SETUP.md.

#include <stddef.h>
#include <stdint.h>

#include "storage.h"

namespace TurnHub {

enum class SetupStage : uint8_t {
  Welcome = 0,   // New or factory-reset Atlas: the screen shows how to start.
  Finished = 1,  // A phone finished the steps: the screen shows "You're all set".
  Complete = 2,  // Normal operation.
};

inline bool validSetupStage(int32_t value) {
  return value >= static_cast<int32_t>(SetupStage::Welcome) &&
      value <= static_cast<int32_t>(SetupStage::Complete);
}

inline const char *setupStageName(SetupStage stage) {
  switch (stage) {
    case SetupStage::Welcome: return "welcome";
    case SetupStage::Finished: return "finished";
    case SetupStage::Complete: return "complete";
  }
  return "complete";
}

TurnHubStorage::Status loadSetupStage(SetupStage &stage);
TurnHubStorage::Status saveSetupStage(SetupStage stage);

// The stage at boot. With no record, an Atlas that already has an Admin was
// set up before this feature and skips it; otherwise it starts at Welcome.
// Unreadable storage reads as Complete, so a storage fault never locks the
// table in setup.
inline SetupStage bootSetupStage(TurnHubStorage::Status loaded, SetupStage stored, bool adminExists) {
  if (loaded == TurnHubStorage::Status::Ok) return stored;
  if (loaded == TurnHubStorage::Status::NotFound) {
    return adminExists ? SetupStage::Complete : SetupStage::Welcome;
  }
  return SetupStage::Complete;
}

// "setup" little-endian image. Schema 1: {1, stage}.
constexpr char SETUP_STAGE_KEY[] = "setup";
constexpr size_t SETUP_STAGE_SIZE = 2;

inline TurnHubStorage::Status readSetupStage(TurnHubStorage::BlobStore &store, SetupStage &stage) {
  using TurnHubStorage::Status;
  uint8_t data[SETUP_STAGE_SIZE] = {};
  size_t size = 0;
  Status status = store.read(SETUP_STAGE_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  status = store.read(SETUP_STAGE_KEY, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  if (data[0] != 1) return Status::UnsupportedSchema;
  if (!validSetupStage(data[1])) return Status::Corrupt;
  stage = static_cast<SetupStage>(data[1]);
  return Status::Ok;
}

inline TurnHubStorage::Status writeSetupStage(TurnHubStorage::BlobStore &store, SetupStage stage) {
  const uint8_t data[SETUP_STAGE_SIZE] = {1, static_cast<uint8_t>(stage)};
  return store.write(SETUP_STAGE_KEY, data, sizeof(data));
}

}  // namespace TurnHub
