#include "profile_policy.h"

namespace TurnHubProfiles {
using TurnHubStorage::Status;

Status readStoredPolicy(TurnHubStorage::BlobStore &store, const char *key,
    ProfilePolicy &policy) {
  size_t size = 0;
  Status status = store.read(key, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != 3) return Status::Corrupt;
  uint8_t bytes[3] = {};
  status = store.read(key, bytes, sizeof(bytes), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(bytes)) return Status::Corrupt;
  if (bytes[0] != 1) return Status::UnsupportedSchema;
  if (bytes[1] > 1 || bytes[2] > 1) return Status::Corrupt;
  policy.allowPhysicalWithoutPin = bytes[1] != 0;
  policy.hideStatsWithoutAuthentication = bytes[2] != 0;
  return Status::Ok;
}

Status writeStoredPolicy(TurnHubStorage::BlobStore &store, const char *key,
    const ProfilePolicy &policy) {
  ProfilePolicy previous;
  const Status status = readStoredPolicy(store, key, previous);
  if (status != Status::Ok && status != Status::NotFound) return status;
  if (status == Status::Ok &&
      previous.allowPhysicalWithoutPin == policy.allowPhysicalWithoutPin &&
      previous.hideStatsWithoutAuthentication == policy.hideStatsWithoutAuthentication)
    return Status::Ok;
  // Explicit byte format: schema, physical-use choice, stats-visibility choice.
  const uint8_t bytes[] = {1, static_cast<uint8_t>(policy.allowPhysicalWithoutPin),
      static_cast<uint8_t>(policy.hideStatsWithoutAuthentication)};
  return store.write(key, bytes, sizeof(bytes));
}
}  // namespace TurnHubProfiles
