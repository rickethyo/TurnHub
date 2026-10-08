#pragma once
#include "storage.h"

namespace TurnHub {

// Whether protected device actions (first Admin, Wi-Fi password, device
// names, updates, Return to lobby, factory reset, tablet mode) also need a
// table presence code from the Atlas screen. Off by default since
// 2026-10-08: a signed-in Admin is enough, and firmware still installs only
// when signed. An Admin can turn it on as an extra step; turning it off again
// needs a code, so a remote Admin cannot quietly drop it.
constexpr bool DEFAULT_TABLE_CODE_REQUIRED = false;

TurnHubStorage::Status loadTableCodeRequired(bool &required);
TurnHubStorage::Status saveTableCodeRequired(bool required);

// "tcode" image. Schema 1: {1, required 0-1}.
constexpr char TABLE_CODE_KEY[] = "tcode";
constexpr size_t TABLE_CODE_SIZE = 2;

inline TurnHubStorage::Status readTableCodeRequired(TurnHubStorage::BlobStore &store, bool &required) {
  using TurnHubStorage::Status;
  uint8_t data[TABLE_CODE_SIZE] = {};
  size_t size = 0;
  Status status = store.read(TABLE_CODE_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  status = store.read(TABLE_CODE_KEY, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  if (data[0] != 1) return Status::UnsupportedSchema;
  if (data[1] > 1) return Status::Corrupt;
  required = data[1] == 1;
  return Status::Ok;
}

inline TurnHubStorage::Status writeTableCodeRequired(TurnHubStorage::BlobStore &store, bool required) {
  const uint8_t data[TABLE_CODE_SIZE] = {1, static_cast<uint8_t>(required ? 1 : 0)};
  return store.write(TABLE_CODE_KEY, data, sizeof(data));
}

}  // namespace TurnHub
