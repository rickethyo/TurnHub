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

// c<profileId>: the statistics every Atlas keeps in NVS, card or not (owner
// decision 2026-09-25). 12 bytes, little-endian: schema 1, last result, last
// game profile, reserved 0, games played (uint32), games won (uint32). The
// full v1 record above is luxury data and lives on the microSD card.
constexpr char CORE_STATS_PREFIX = 'c';
struct CoreStats {
  uint32_t gamesPlayed = 0;
  uint32_t gamesWon = 0;
  LastGameResult lastGameResult = LastGameResult::None;
  uint8_t lastGameProfile = 0;
};
inline CoreStats coreOf(const ProfileStats &stats) {
  CoreStats core;
  core.gamesPlayed = stats.gamesPlayed;
  core.gamesWon = stats.gamesWon;
  core.lastGameResult = stats.lastGameResult;
  core.lastGameProfile = stats.lastGameProfile;
  return core;
}
TurnHubStorage::Status readCoreStats(TurnHubStorage::BlobStore &store, const char *key, CoreStats &stats);
TurnHubStorage::Status writeCoreStats(TurnHubStorage::BlobStore &store, const char *key, const CoreStats &stats);

// o<profileId>: {schema 1, connectionResets[4], gameRemovals[4]}, little-endian.
// Like the statistics record, an unreadable record is never overwritten.
constexpr char MODERATION_STATS_PREFIX = 'o';
TurnHubStorage::Status readStoredModerationStats(TurnHubStorage::BlobStore &store,
                                                const char *key, ModerationStats &stats);
TurnHubStorage::Status writeStoredModerationStats(TurnHubStorage::BlobStore &store,
                                                 const char *key, const ModerationStats &stats);

}  // namespace TurnHubProfiles
