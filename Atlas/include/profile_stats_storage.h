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

// o<profileId>: {schema 1, connectionResets[4], gameRemovals[4]}, little-endian.
// Like the statistics record, an unreadable record is never overwritten.
constexpr char MODERATION_STATS_PREFIX = 'o';
TurnHubStorage::Status readStoredModerationStats(TurnHubStorage::BlobStore &store,
                                                const char *key, ModerationStats &stats);
TurnHubStorage::Status writeStoredModerationStats(TurnHubStorage::BlobStore &store,
                                                 const char *key, const ModerationStats &stats);

}  // namespace TurnHubProfiles
