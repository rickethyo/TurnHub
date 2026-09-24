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
      static_cast<uint8_t>(LastGameResult::Draw)) return Status::Corrupt;
  stats = stored;
  return Status::Ok;
}

Status writeStoredStats(TurnHubStorage::BlobStore &store, const char *key,
                        const ProfileStats &stats) {
  if (stats.schemaVersion != STATS_SCHEMA_VERSION) return Status::UnsupportedSchema;
  if (static_cast<uint8_t>(stats.lastGameResult) >
      static_cast<uint8_t>(LastGameResult::Draw)) return Status::Corrupt;
  ProfileStats existing{};
  const Status status = readStoredStats(store, key, existing);
  if (status != Status::Ok && status != Status::NotFound) return status;
  return store.write(key, &stats, sizeof(stats));
}

namespace {
constexpr uint8_t MODERATION_SCHEMA = 1;
constexpr size_t MODERATION_RECORD_SIZE = 9;

uint32_t readLe32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe32(uint8_t *data, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) data[i] = static_cast<uint8_t>(value >> (8 * i));
}
}  // namespace

Status readStoredModerationStats(TurnHubStorage::BlobStore &store, const char *key,
                                 ModerationStats &stats) {
  size_t size = 0;
  Status status = store.read(key, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != MODERATION_RECORD_SIZE) return Status::Corrupt;
  uint8_t data[MODERATION_RECORD_SIZE] = {};
  status = store.read(key, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  if (data[0] != MODERATION_SCHEMA) return Status::UnsupportedSchema;
  stats.connectionResets = readLe32(data + 1);
  stats.gameRemovals = readLe32(data + 5);
  return Status::Ok;
}

Status writeStoredModerationStats(TurnHubStorage::BlobStore &store, const char *key,
                                  const ModerationStats &stats) {
  ModerationStats existing;
  const Status status = readStoredModerationStats(store, key, existing);
  if (status != Status::Ok && status != Status::NotFound) return status;
  uint8_t data[MODERATION_RECORD_SIZE] = {MODERATION_SCHEMA};
  writeLe32(data + 1, stats.connectionResets);
  writeLe32(data + 5, stats.gameRemovals);
  return store.write(key, data, sizeof(data));
}
}  // namespace TurnHubProfiles
