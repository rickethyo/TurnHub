#pragma once
#include "storage.h"

namespace TurnHubProfiles {
// Defaults preserve existing physical play while keeping statistics private.
struct ProfilePolicy {
  bool allowPhysicalWithoutPin = true;
  bool hideStatsWithoutAuthentication = true;
};

TurnHubStorage::Status readStoredPolicy(TurnHubStorage::BlobStore &store,
    const char *key, ProfilePolicy &policy);
TurnHubStorage::Status writeStoredPolicy(TurnHubStorage::BlobStore &store,
    const char *key, const ProfilePolicy &policy);
}  // namespace TurnHubProfiles
