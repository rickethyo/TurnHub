#include "profile_stats_storage.h"
#include <stddef.h>
#include <type_traits>

namespace TurnHubProfiles {
using TurnHubStorage::Status;

// v1 is the ESP32's deployed struct image, including four trailing padding
// bytes. Freeze its ABI until an explicit versioned migration replaces it.
static_assert(sizeof(ProfileStats) == 72, "v1 statistics size changed");
static_assert(offsetof(ProfileStats, schemaVersion) == 64, "v1 schema offset changed");
static_assert(offsetof(ProfileStats, lastGameResult) == 66, "v1 result offset changed");
static_assert(std::is_trivially_copyable<ProfileStats>::value, "v1 must remain POD");

Status readStoredStats(TurnHubStorage::BlobStore &store, const char *key,
                       ProfileStats &stats) {
  size_t size = 0;
  Status status = store.read(key, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != sizeof(ProfileStats)) return Status::Corrupt;
  ProfileStats stored{};
  status = store.read(key, &stored, sizeof(stored), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(stored)) return Status::Corrupt;
  if (stored.schemaVersion != STATS_SCHEMA_VERSION) return Status::UnsupportedSchema;
  if (static_cast<uint8_t>(stored.lastGameResult) >
      static_cast<uint8_t>(LastGameResult::Completed)) return Status::Corrupt;
  stats = stored;
  return Status::Ok;
}

Status writeStoredStats(TurnHubStorage::BlobStore &store, const char *key,
                        const ProfileStats &stats) {
  if (stats.schemaVersion != STATS_SCHEMA_VERSION) return Status::UnsupportedSchema;
  if (static_cast<uint8_t>(stats.lastGameResult) >
      static_cast<uint8_t>(LastGameResult::Completed)) return Status::Corrupt;
  ProfileStats existing{};
  const Status status = readStoredStats(store, key, existing);
  if (status != Status::Ok && status != Status::NotFound) return status;
  return store.write(key, &stats, sizeof(stats));
}
}  // namespace TurnHubProfiles
