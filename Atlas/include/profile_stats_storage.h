#pragma once

#include "profile_store.h"
#include "storage.h"

namespace TurnHubProfiles {

// The schema 4 blob under s<profileId>. Unknown or damaged records are never reset or
// overwritten.
TurnHubStorage::Status readStoredStats(TurnHubStorage::BlobStore &store,
                                      const char *key, ProfileStats &stats);
TurnHubStorage::Status writeStoredStats(TurnHubStorage::BlobStore &store,
                                       const char *key, const ProfileStats &stats);

// c<profileId>: the statistics every Atlas keeps in NVS, card or not (owner
// decision 2026-09-25). 28 bytes, little-endian: schema 3, last result, last
// game profile, reserved 0, games played, games won, Star Commander games
// played and won, lifetime and last-game eliminations (uint32 each). The full record above is luxury data and
// lives on the microSD card.
constexpr char CORE_STATS_PREFIX = 'c';
struct CoreStats {
  uint32_t gamesPlayed = 0;
  uint32_t gamesWon = 0;
  LastGameResult lastGameResult = LastGameResult::None;
  uint8_t lastGameProfile = 0;
  uint32_t starGamesPlayed = 0;
  uint32_t starGamesWon = 0;
  uint32_t eliminationsMade = 0;
  uint32_t lastGameEliminationsMade = 0;
};
inline CoreStats coreOf(const ProfileStats &stats) {
  CoreStats core;
  core.gamesPlayed = stats.gamesPlayed;
  core.gamesWon = stats.gamesWon;
  core.lastGameResult = stats.lastGameResult;
  core.lastGameProfile = stats.lastGameProfile;
  core.starGamesPlayed = stats.starGamesPlayed;
  core.starGamesWon = stats.starGamesWon;
  core.eliminationsMade = stats.eliminationsMade;
  core.lastGameEliminationsMade = stats.lastGameEliminationsMade;
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
