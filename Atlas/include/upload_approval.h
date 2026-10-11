#pragma once
#include "storage.h"

namespace TurnHub {
constexpr bool DEFAULT_UPLOAD_APPROVAL_REQUIRED = true;
constexpr char UPLOAD_APPROVAL_KEY[] = "imgreview";
TurnHubStorage::Status loadUploadApprovalRequired(bool &required);
TurnHubStorage::Status saveUploadApprovalRequired(bool required);
inline TurnHubStorage::Status readUploadApprovalRequired(TurnHubStorage::BlobStore &store, bool &required) {
  using TurnHubStorage::Status;
  uint8_t bytes[2] = {}; size_t size = 0;
  Status status = store.read(UPLOAD_APPROVAL_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != sizeof(bytes)) return Status::Corrupt;
  status = store.read(UPLOAD_APPROVAL_KEY, bytes, sizeof(bytes), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(bytes) || bytes[1] > 1) return Status::Corrupt;
  if (bytes[0] != 1) return Status::UnsupportedSchema;
  required = bytes[1] == 1;
  return Status::Ok;
}
inline TurnHubStorage::Status writeUploadApprovalRequired(TurnHubStorage::BlobStore &store, bool required) {
  const uint8_t bytes[] = {1, static_cast<uint8_t>(required)};
  return store.write(UPLOAD_APPROVAL_KEY, bytes, sizeof(bytes));
}
}
