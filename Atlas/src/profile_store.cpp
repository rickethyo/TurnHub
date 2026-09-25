#include "profile_store.h"
#include "account_access.h"

#include "optional_preferences.h"
#include "nvs_blob_store.h"
#include "profile_stats_storage.h"
#include <esp_system.h>
#include <string.h>

namespace TurnHubProfiles {
namespace {

constexpr char PREF_NAMESPACE[] = "turnhub";

TurnHub::OptionalPreferences preferences;
TurnHubStorage::NvsBlobStore statsStorage;
// The microSD card's store for luxury records, or nullptr (limp mode).
TurnHubStorage::BlobStore *luxuryStore = nullptr;
bool preferencesReady = false;
constexpr uint8_t TRANSIENT_SEAT_CAPACITY = 16;
struct TransientSeatBinding {
  bool used = false;
  uint8_t mac[6] = {};
  uint8_t slot = 0;
  String profileId;
};
TransientSeatBinding transientSeats[TRANSIENT_SEAT_CAPACITY];

TransientSeatBinding *transientSeatFor(const uint8_t mac[6], uint8_t slot, bool create) {
  for (auto &binding : transientSeats) {
    if (binding.used && binding.slot == slot && memcmp(binding.mac, mac, 6) == 0) return &binding;
  }
  if (!create) return nullptr;
  for (auto &binding : transientSeats) {
    if (!binding.used) {
      binding.used = true;
      binding.slot = slot;
      memcpy(binding.mac, mac, 6);
      binding.profileId = String();
      return &binding;
    }
  }
  return nullptr;
}

String seatKey(char prefix, const uint8_t mac[6], uint8_t slot) {
  char key[16];
  snprintf(
      key,
      sizeof(key),
      "%c%02X%02X%02X%02X%02X%02X%c",
      prefix,
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
      slot == 1 ? 'A' : 'B');
  return String(key);
}

String deviceNameKey(const uint8_t mac[6]) {
  char key[14];
  snprintf(
      key,
      sizeof(key),
      "d%02X%02X%02X%02X%02X%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(key);
}

String profileKey(char prefix, const String &profileId) {
  String key;
  key.reserve(1 + PROFILE_ID_LENGTH);
  key += prefix;
  key += profileId;
  return key;
}

bool validProfileId(const String &profileId) {
  TurnHubIdentity::ProfileId parsed;
  return profileId.length() == PROFILE_ID_LENGTH &&
      TurnHubIdentity::ProfileId::parse(profileId.c_str(), parsed);
}

String makeProfileId() {
  char id[PROFILE_ID_LENGTH + 1];
  snprintf(id, sizeof(id), "%08lX", static_cast<unsigned long>(esp_random()));
  return String(id);
}

bool ensureMarker(const String &profileId) {
  if (!preferencesReady || !validProfileId(profileId)) {
    return false;
  }
  const String markerKey = profileKey('m', profileId);
  if (preferences.isKey(markerKey.c_str())) {
    return true;
  }
  return preferences.putUChar(markerKey.c_str(), 1) != 0;
}

void migrateLegacyName(
    const uint8_t mac[6],
    uint8_t slot,
    const String &profileId) {
  if (!validProfileId(profileId) || nameForProfile(profileId).length() > 0) {
    return;
  }

  const String legacyKey = seatKey('n', mac, slot);
  const String legacy = preferences.getString(legacyKey.c_str(), "");
  if (legacy.length() > 0 && setNameForProfile(profileId, legacy)) {
    preferences.remove(legacyKey.c_str());
  }
}

}  // namespace

bool begin() {
  if (preferencesReady) {
    return true;
  }
  preferencesReady = preferences.begin(PREF_NAMESPACE, false);
  return preferencesReady;
}

bool ready() {
  return preferencesReady;
}

String createProfile() {
  if (!preferencesReady && !begin()) {
    return String();
  }

  for (uint8_t attempt = 0; attempt < 16; ++attempt) {
    const String profileId = makeProfileId();
    if (!profileExists(profileId) && ensureMarker(profileId)) {
      return profileId;
    }
  }
  return String();
}

bool profileExists(const String &profileId) {
  if (!preferencesReady || !validProfileId(profileId)) {
    return false;
  }
  return preferences.isKey(profileKey('m', profileId).c_str());
}

size_t listProfileIds(char (*ids)[PROFILE_ID_LENGTH + 1], size_t capacity) {
  if (!preferencesReady || ids == nullptr) return 0;
  size_t count = 0;
  nvs_iterator_t it = nvs_entry_find("nvs", PREF_NAMESPACE, NVS_TYPE_U8);
  while (it != nullptr && count < capacity) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (info.key[0] == 'm' && validProfileId(String(info.key + 1))) {
      // Older display syncs created marker-only profiles for unused seats.
      // Keep their records/bindings intact, but do not let these placeholders
      // crowd out real accounts or exhaust the registration limit.
      const String id(info.key + 1);
      bool configured = false;
      for (const char prefix : {'n', 'p', 's', 'a', 'u', MODERATION_STATS_PREFIX, ACCESSIBILITY_PREFIX}) {
        configured = configured || preferences.isKey(profileKey(prefix, id).c_str());
      }
      if (configured) memcpy(ids[count++], info.key + 1, PROFILE_ID_LENGTH + 1);
    }
    it = nvs_entry_next(it);
  }
  nvs_release_iterator(it);
  return count;
}

String createProfileWithCredentials(const String &name, const String &pin, PinHasher hasher) {
  if ((!preferencesReady && !begin()) || name.length() == 0 || name.length() > 32 || hasher == nullptr) return String();
  char ids[MAX_LOGIN_PROFILES][PROFILE_ID_LENGTH + 1];
  if (listProfileIds(ids, MAX_LOGIN_PROFILES) >= MAX_LOGIN_PROFILES) return String();
  for (uint8_t attempt = 0; attempt < 16; ++attempt) {
    const String id = makeProfileId();
    if (profileExists(id)) continue;
    const String hash = hasher(id, pin);
    if (hash.length() != 64) return String();
    const String nameKey = profileKey('n', id), pinKey = profileKey('p', id);
    // Publish the profile marker last. Partial setup is never a login identity.
    if (preferences.putString(nameKey.c_str(), name) > 0 &&
        preferences.putString(pinKey.c_str(), hash) > 0 && ensureMarker(id)) return id;
    preferences.remove(nameKey.c_str());
    preferences.remove(pinKey.c_str());
    return String();
  }
  return String();
}

String nameForProfile(const String &profileId) {
  if (!preferencesReady || !profileExists(profileId)) {
    return String();
  }
  return preferences.getString(profileKey('n', profileId).c_str(), "");
}

bool setNameForProfile(const String &profileId, const String &name) {
  if (!preferencesReady || !profileExists(profileId)) {
    return false;
  }
  const String key = profileKey('n', profileId);
  if (name.length() == 0) {
    preferences.remove(key.c_str());
    return true;
  }
  if (preferences.getString(key.c_str(), "") == name) return true;
  return preferences.putString(key.c_str(), name) > 0;
}

String storedPinHashForProfile(const String &profileId) {
  if (!preferencesReady || !profileExists(profileId)) {
    return String();
  }
  return preferences.getString(profileKey('p', profileId).c_str(), "");
}

bool setPinHashForProfile(const String &profileId, const String &hash) {
  if (!preferencesReady || !profileExists(profileId) || hash.length() != 64) {
    return false;
  }
  const String key = profileKey('p', profileId);
  if (preferences.getString(key.c_str(), "") == hash) return true;
  return preferences.putString(key.c_str(), hash) > 0;
}

bool clearPinForProfile(const String &profileId) {
  if (!preferencesReady || !profileExists(profileId)) {
    return false;
  }
  preferences.remove(profileKey('p', profileId).c_str());
  return true;
}

bool hasPinForProfile(const String &profileId) {
  return storedPinHashForProfile(profileId).length() == 64;
}

bool loadPolicyForProfile(const String &profileId, ProfilePolicy &policy) {
  // Fail closed. Only a genuinely absent record receives compatibility defaults.
  policy.allowPhysicalWithoutPin = false;
  policy.hideStatsWithoutAuthentication = true;
  if (!preferencesReady || !profileExists(profileId) ||
      statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  const auto status = readStoredPolicy(statsStorage, profileKey('a', profileId).c_str(), policy);
  if (status == TurnHubStorage::Status::NotFound) {
    policy = ProfilePolicy{};
    return true;
  }
  return status == TurnHubStorage::Status::Ok;
}

bool savePolicyForProfile(const String &profileId, const ProfilePolicy &policy) {
  if (!preferencesReady || !profileExists(profileId) ||
      (!policy.allowPhysicalWithoutPin && !hasPinForProfile(profileId)) ||
      statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  return writeStoredPolicy(statsStorage, profileKey('a', profileId).c_str(), policy) ==
      TurnHubStorage::Status::Ok;
}

bool loadAccessibilityForProfile(const String &profileId, AccessibilityPrefs &prefs) {
  // A missing record means the defaults; an unreadable one keeps the defaults
  // for presentation but reports failure so the portal can say so.
  prefs = AccessibilityPrefs{};
  if (!preferencesReady || !profileExists(profileId) ||
      statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  const auto status = readAccessibilityPrefs(statsStorage,
      profileKey(ACCESSIBILITY_PREFIX, profileId).c_str(), prefs);
  if (status == TurnHubStorage::Status::NotFound) return true;
  if (status != TurnHubStorage::Status::Ok) prefs = AccessibilityPrefs{};
  return status == TurnHubStorage::Status::Ok;
}

namespace {
// Jewel colour cache: profile ID -> colour or none, so the Sigil sync loop
// never reads the card more than once per profile.
struct JewelCacheEntry {
  char id[PROFILE_ID_LENGTH + 1] = {};
  bool set = false;
  uint32_t rgb = 0;
};
JewelCacheEntry jewelCache[16];
uint8_t jewelCacheNext = 0;
JewelCacheEntry *cachedJewel(const String &profileId) {
  for (auto &entry : jewelCache) {
    if (entry.id[0] && profileId == entry.id) return &entry;
  }
  return nullptr;
}
}  // namespace

bool jewelColorForProfile(const String &profileId, uint32_t &rgb) {
  if (profileId.length() != PROFILE_ID_LENGTH || luxuryStore == nullptr) return false;
  JewelCacheEntry *entry = cachedJewel(profileId);
  if (entry == nullptr) {
    entry = &jewelCache[jewelCacheNext];
    jewelCacheNext = static_cast<uint8_t>((jewelCacheNext + 1) % 16);
    *entry = JewelCacheEntry{};
    strncpy(entry->id, profileId.c_str(), PROFILE_ID_LENGTH);
    uint8_t record[4] = {};
    size_t size = 0;
    if (luxuryStore->read(profileKey('k', profileId).c_str(), record, sizeof(record), size) ==
            TurnHubStorage::Status::Ok && size == sizeof(record) && record[0] == 1) {
      entry->set = true;
      entry->rgb = (static_cast<uint32_t>(record[1]) << 16) | (static_cast<uint32_t>(record[2]) << 8) | record[3];
    }
  }
  rgb = entry->rgb;
  return entry->set;
}

bool saveJewelColorForProfile(const String &profileId, bool set, uint32_t rgb) {
  if (luxuryStore == nullptr || !profileExists(profileId)) return false;
  const String key = profileKey('k', profileId);
  TurnHubStorage::Status status;
  if (set) {
    const uint8_t record[4] = {1, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
        static_cast<uint8_t>(rgb)};
    status = luxuryStore->write(key.c_str(), record, sizeof(record));
  } else {
    status = luxuryStore->remove(key.c_str());
    if (status == TurnHubStorage::Status::NotFound) status = TurnHubStorage::Status::Ok;
  }
  if (JewelCacheEntry *entry = cachedJewel(profileId)) *entry = JewelCacheEntry{};
  return status == TurnHubStorage::Status::Ok;
}

bool saveAccessibilityForProfile(const String &profileId, const AccessibilityPrefs &prefs) {
  if (!preferencesReady || !profileExists(profileId) || !validAccessibilityPrefs(prefs) ||
      statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  return writeAccessibilityPrefs(statsStorage,
      profileKey(ACCESSIBILITY_PREFIX, profileId).c_str(), prefs) == TurnHubStorage::Status::Ok;
}

void setLuxuryStore(TurnHubStorage::BlobStore *store) { luxuryStore = store; }

bool luxuryStoreAvailable() { return luxuryStore != nullptr; }

bool loadStatsForProfile(const String &profileId, ProfileStats &stats, bool *detailed) {
  using TurnHubStorage::Status;
  stats = ProfileStats{};
  if (detailed) *detailed = false;
  if (!preferencesReady || !profileExists(profileId)) {
    return false;
  }
  if (statsStorage.begin(PREF_NAMESPACE) != Status::Ok) return false;

  // NVS: the core record, and a v1 record older firmware left (the detail
  // until it moves to the card). A fresh profile has neither. Any other
  // failure must stop the completion callback from replacing an unreadable
  // record with zero totals.
  CoreStats core;
  const Status coreStatus = readCoreStats(statsStorage, profileKey(CORE_STATS_PREFIX, profileId).c_str(), core);
  if (coreStatus != Status::Ok && coreStatus != Status::NotFound) return false;
  ProfileStats legacy{};
  const Status legacyStatus = readStoredStats(statsStorage, profileKey('s', profileId).c_str(), legacy);
  if (legacyStatus != Status::Ok && legacyStatus != Status::NotFound) return false;

  if (legacyStatus == Status::Ok) {
    stats = legacy;
    if (detailed) *detailed = true;
  } else if (luxuryStore != nullptr) {
    // A card problem only costs the detail; the core counts still load.
    ProfileStats detail{};
    const Status detailStatus = readStoredStats(*luxuryStore, profileKey('s', profileId).c_str(), detail);
    if (detailStatus == Status::Ok) stats = detail;
    if (detailed) *detailed = detailStatus == Status::Ok || detailStatus == Status::NotFound;
  }
  // The core record is authoritative for its fields: it keeps counting while
  // the detail is missing (no card), so the detail's copies can lag.
  if (coreStatus == Status::Ok) {
    stats.gamesPlayed = core.gamesPlayed;
    stats.gamesWon = core.gamesWon;
    stats.lastGameResult = core.lastGameResult;
    stats.lastGameProfile = core.lastGameProfile;
  }
  return true;
}

bool saveStatsForProfile(const String &profileId, const ProfileStats &stats) {
  using TurnHubStorage::Status;
  if (!preferencesReady || !profileExists(profileId) ||
      statsStorage.begin(PREF_NAMESPACE) != Status::Ok) {
    return false;
  }
  const String detailKey = profileKey('s', profileId);
  if (writeCoreStats(statsStorage, profileKey(CORE_STATS_PREFIX, profileId).c_str(), coreOf(stats)) !=
      Status::Ok) {
    return false;
  }
  ProfileStats legacy{};
  const bool legacyInNvs = readStoredStats(statsStorage, detailKey.c_str(), legacy) == Status::Ok;
  if (luxuryStore != nullptr && writeStoredStats(*luxuryStore, detailKey.c_str(), stats) == Status::Ok) {
    // The card has the detail now; free the NVS copy.
    if (legacyInNvs) statsStorage.remove(detailKey.c_str());
    return true;
  }
  // No card (or it failed): an NVS detail record from older firmware stays
  // current in place (same size); otherwise only the core counts are kept.
  if (legacyInNvs) writeStoredStats(statsStorage, detailKey.c_str(), stats);
  return true;
}

size_t migrateDetailedStats() {
  using TurnHubStorage::Status;
  if (luxuryStore == nullptr || !preferencesReady ||
      statsStorage.begin(PREF_NAMESPACE) != Status::Ok) {
    return 0;
  }
  char ids[MAX_LOGIN_PROFILES][PROFILE_ID_LENGTH + 1];
  const size_t count = listProfileIds(ids, MAX_LOGIN_PROFILES);
  size_t moved = 0;
  for (size_t i = 0; i < count; ++i) {
    const String id(ids[i]);
    const String detailKey = profileKey('s', id);
    ProfileStats legacy{};
    if (readStoredStats(statsStorage, detailKey.c_str(), legacy) != Status::Ok) continue;
    // Keep the core record ahead of the detail: the counts must never be
    // lost between the two writes.
    const String coreKey = profileKey(CORE_STATS_PREFIX, id);
    CoreStats core;
    const Status coreStatus = readCoreStats(statsStorage, coreKey.c_str(), core);
    if (coreStatus == Status::NotFound &&
        writeCoreStats(statsStorage, coreKey.c_str(), coreOf(legacy)) != Status::Ok) continue;
    if (coreStatus != Status::Ok && coreStatus != Status::NotFound) continue;
    // A detail record already on the card (this card was used before) is
    // never overwritten by an older NVS copy; both stay for a person to sort.
    ProfileStats onCard{};
    const Status cardStatus = readStoredStats(*luxuryStore, detailKey.c_str(), onCard);
    if (cardStatus == Status::NotFound) {
      if (writeStoredStats(*luxuryStore, detailKey.c_str(), legacy) != Status::Ok) continue;
      if (readStoredStats(*luxuryStore, detailKey.c_str(), onCard) != Status::Ok) continue;
    } else if (cardStatus != Status::Ok) {
      continue;
    }
    if (memcmp(&onCard, &legacy, sizeof(legacy)) != 0) continue;
    if (statsStorage.remove(detailKey.c_str()) == Status::Ok) ++moved;
  }
  return moved;
}

bool loadModerationStatsForProfile(const String &profileId, ModerationStats &stats) {
  stats = ModerationStats{};
  // Loading the account first migrates any counts it still holds.
  TurnHubAccounts::Account account;
  if (!preferencesReady || !TurnHubAccounts::load(profileId, account)) return false;
  if (statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  const String key = profileKey(MODERATION_STATS_PREFIX, profileId);
  const auto status = readStoredModerationStats(statsStorage, key.c_str(), stats);
  return status == TurnHubStorage::Status::Ok || status == TurnHubStorage::Status::NotFound;
}

bool saveModerationStatsForProfile(const String &profileId, const ModerationStats &stats) {
  if (!preferencesReady || !profileExists(profileId) ||
      statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  const String key = profileKey(MODERATION_STATS_PREFIX, profileId);
  return writeStoredModerationStats(statsStorage, key.c_str(), stats) == TurnHubStorage::Status::Ok;
}

String profileIdForSeat(const uint8_t mac[6], uint8_t slot) {
  // Looking up a Sigil, including its unused secondary seat, must never
  // manufacture a durable account. Unbound seats are guests.
  const String profileId = boundProfileIdForSeat(mac, slot);
  migrateLegacyName(mac, slot, profileId);
  return profileId;
}

String boundProfileIdForSeat(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady && !begin()) {
    return String();
  }
  if (slot != 1 && slot != 2) {
    return String();
  }
  const auto *binding = transientSeatFor(mac, slot, false);
  return binding && validProfileId(binding->profileId) && profileExists(binding->profileId)
      ? binding->profileId : String();
}

bool resetTransientSeatBindings(const uint8_t mac[6]) {
  if (!preferencesReady && !begin()) return false;
  bool ok = true;
  for (uint8_t slot : {1, 2}) {
    if (auto *binding = transientSeatFor(mac, slot, false)) {
      binding->used = false;
      binding->profileId = String();
    }
    // Retire legacy remembered-seat keys, without deleting Atlas accounts/stats.
    for (char prefix : {'b', 'r'}) {
      const String key = seatKey(prefix, mac, slot);
      if (preferences.isKey(key.c_str())) ok = preferences.remove(key.c_str()) && ok;
    }
  }
  return ok;
}

bool moveSeatProfile(const uint8_t mac[6], uint8_t fromSlot, uint8_t toSlot, const String &profileId) {
  if ((fromSlot != 1 && fromSlot != 2) || toSlot != 3 - fromSlot ||
      boundProfileIdForSeat(mac, fromSlot) != profileId ||
      boundProfileIdForSeat(mac, toSlot).length()) return false;
  if (!bindSeatToProfile(mac, toSlot, profileId)) return false;
  auto *source = transientSeatFor(mac, fromSlot, false);
  source->used = false;
  source->profileId = String();
  return true;
}

bool bindSeatToProfile(const uint8_t mac[6], uint8_t slot, const String &profileId) {
  if (!preferencesReady || (slot != 1 && slot != 2) || !profileExists(profileId)) return false;
  auto *binding = transientSeatFor(mac, slot, true);
  if (!binding) return false;
  binding->profileId = profileId;
  return true;
}

String nameForSeat(const uint8_t mac[6], uint8_t slot) {
  const String profileId = profileIdForSeat(mac, slot);
  return validProfileId(profileId) ? nameForProfile(profileId) : String();
}

bool setNameForSeat(const uint8_t mac[6], uint8_t slot, const String &name) {
  const String profileId = profileIdForSeat(mac, slot);
  if (!validProfileId(profileId)) {
    return false;
  }
  const bool ok = setNameForProfile(profileId, name);
  if (ok) {
    preferences.remove(seatKey('n', mac, slot).c_str());
  }
  return ok;
}

String storedPinHashForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    bool *legacySource) {
  if (legacySource != nullptr) {
    *legacySource = false;
  }
  if (!preferencesReady && !begin()) {
    return String();
  }

  const String profileId = profileIdForSeat(mac, slot);
  if (validProfileId(profileId)) {
    const String stored = storedPinHashForProfile(profileId);
    if (stored.length() == 64) {
      return stored;
    }
  }

  const String legacy = preferences.getString(seatKey('p', mac, slot).c_str(), "");
  if (legacy.length() == 64 && legacySource != nullptr) {
    *legacySource = true;
  }
  return legacy;
}

bool setPinHashForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    const String &hash) {
  const String profileId = profileIdForSeat(mac, slot);
  if (!validProfileId(profileId)) {
    return false;
  }
  const bool ok = setPinHashForProfile(profileId, hash);
  if (ok) {
    preferences.remove(seatKey('p', mac, slot).c_str());
  }
  return ok;
}

bool clearPinForSeat(const uint8_t mac[6], uint8_t slot) {
  const String profileId = profileIdForSeat(mac, slot);
  if (validProfileId(profileId)) {
    clearPinForProfile(profileId);
  }
  preferences.remove(seatKey('p', mac, slot).c_str());
  return true;
}

bool hasPinForSeat(const uint8_t mac[6], uint8_t slot) {
  return storedPinHashForSeat(mac, slot, nullptr).length() == 64;
}

String deviceName(const uint8_t mac[6]) {
  if (!preferencesReady && !begin()) {
    return String();
  }
  return preferences.getString(deviceNameKey(mac).c_str(), "");
}

bool setDeviceName(const uint8_t mac[6], const String &name) {
  if (!preferencesReady && !begin()) {
    return false;
  }
  const String key = deviceNameKey(mac);
  if (name.length() == 0) {
    preferences.remove(key.c_str());
    return true;
  }
  if (preferences.getString(key.c_str(), "") == name) return true;
  return preferences.putString(key.c_str(), name) > 0;
}

bool loadStatsForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    ProfileStats &stats) {
  const String profileId = profileIdForSeat(mac, slot);
  return validProfileId(profileId) && loadStatsForProfile(profileId, stats);
}

bool saveStatsForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    const ProfileStats &stats) {
  const String profileId = profileIdForSeat(mac, slot);
  return validProfileId(profileId) && saveStatsForProfile(profileId, stats);
}

}  // namespace TurnHubProfiles

namespace TurnHubAccounts {

namespace {

using TurnHubStorage::Status;

String accountKey(const String &id) {
  return String("u") + id;
}

// Moves counts that older firmware kept in the account record into the
// profile's moderation statistics, then clears them from the account. The
// statistics record is written first; if power fails before the account is
// cleared, the next load sees an existing record and only clears the account.
bool migrateLegacyModerationCounts(TurnHubStorage::BlobStore &store, const String &id, Account &account) {
  String statsKey;
  statsKey += TurnHubProfiles::MODERATION_STATS_PREFIX;
  statsKey += id;
  TurnHubProfiles::ModerationStats stats;
  const Status status = TurnHubProfiles::readStoredModerationStats(store, statsKey.c_str(), stats);
  if (status == Status::NotFound) {
    stats.connectionResets = account.legacyConnectionResets;
    stats.gameRemovals = account.legacyGameRemovals;
    if (TurnHubProfiles::writeStoredModerationStats(store, statsKey.c_str(), stats) != Status::Ok) return false;
  } else if (status != Status::Ok) {
    return false;  // Never guess over an unreadable record.
  }
  account.legacyConnectionResets = 0;
  account.legacyGameRemovals = 0;
  return write(store, accountKey(id).c_str(), account) == Status::Ok;
}

}  // namespace

bool load(const String &id, Account &account) {
  if (!TurnHubProfiles::profileExists(id)) return false;
  TurnHubStorage::NvsBlobStore store;
  if (store.begin("turnhub") != Status::Ok) return false;
  const Status status = read(store, accountKey(id).c_str(), account);
  if (status == Status::NotFound) {
    account = Account{};
  } else if (status != Status::Ok) {
    return false;
  } else if ((account.legacyConnectionResets || account.legacyGameRemovals) &&
      !migrateLegacyModerationCounts(store, id, account)) {
    return false;
  }
  String primary;
  if (!primaryAdmin(primary)) return false;
  if (id == primary) account.permissions |= Admin;
  return true;
}

bool save(const String &id, const Account &account) {
  if (!TurnHubProfiles::profileExists(id)) return false;
  TurnHubStorage::NvsBlobStore store;
  return store.begin("turnhub") == Status::Ok && write(store, accountKey(id).c_str(), account) == Status::Ok;
}

bool primaryAdmin(String &id){
  id="";TurnHubStorage::NvsBlobStore store;if(store.begin("turnhub")!=TurnHubStorage::Status::Ok)return false;
  size_t n=0;auto s=store.read("acctadmin",nullptr,0,n);
  if(s==TurnHubStorage::Status::NotFound)return true;
  if(s!=TurnHubStorage::Status::Ok||n!=9)return false;
  char b[9]={};s=store.read("acctadmin",b,sizeof(b),n);
  if(s!=TurnHubStorage::Status::Ok||n!=9||b[8]!=0||!TurnHubProfiles::profileExists(String(b)))return false;
  id=String(b);return true;
}
bool establishAdmin(const String &id){
  String current;if(!primaryAdmin(current)||current.length()||!TurnHubProfiles::hasPinForProfile(id))return false;
  Account a;if(!load(id,a))return false;
  TurnHubStorage::NvsBlobStore store;return store.begin("turnhub")==TurnHubStorage::Status::Ok&&store.write("acctadmin",id.c_str(),9)==TurnHubStorage::Status::Ok;
}
}
