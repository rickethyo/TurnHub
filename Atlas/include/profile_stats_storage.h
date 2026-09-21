#pragma once

#include "profile_store.h"
#include "storage.h"

namespace TurnHubProfiles {

// Preserves the deployed v1 blob and s<profileId> key. No schema migration is
// performed here. Unknown or damaged records are never reset or overwritten.
TurnHubStorage::Status readStoredStats(TurnHubStorage::BlobStore &store,
                                      const char *key, ProfileStats &stats);
TurnHubStorage::Status writeStoredStats(TurnHubStorage::BlobStore &store,
                                       const char *key, const ProfileStats &stats);

}  // namespace TurnHubProfiles
