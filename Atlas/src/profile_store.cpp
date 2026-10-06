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

// A named profile with no PIN, as a table tablet seats a new player.
String createProfileWithName(const String &name) {
  if ((!preferencesReady && !begin()) || name.length() == 0 || name.length() > 32) return String();
  char ids[MAX_LOGIN_PROFILES][PROFILE_ID_LENGTH + 1];
  if (listProfileIds(ids, MAX_LOGIN_PROFILES) >= MAX_LOGIN_PROFILES) return String();
  for (uint8_t attempt = 0; attempt < 16; ++attempt) {
    const String id = makeProfileId();
    if (profileExists(id)) continue;
    const String nameKey = profileKey('n', id);
    // Publish the profile marker last, as createProfileWithCredentials does.
    if (preferences.putString(nameKey.c_str(), name) > 0 && ensureMarker(id)) return id;
    preferences.remove(nameKey.c_str());
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
// Personalization cache: profile ID -> Jewel color and avatar, so the Sigil
// sync loop and /api/seats never read the card more than once per profile.
struct JewelCacheEntry {
  char id[PROFILE_ID_LENGTH + 1] = {};
  bool set = false;
  uint32_t rgb = 0;
  uint8_t avatar = 0;
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

static JewelCacheEntry *personalization(const String &profileId) {
  if (profileId.length() != PROFILE_ID_LENGTH || luxuryStore == nullptr) return nullptr;
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
    uint8_t avatar[2] = {};
    if (luxuryStore->read(profileKey('v', profileId).c_str(), avatar, sizeof(avatar), size) ==
            TurnHubStorage::Status::Ok && size == sizeof(avatar) && avatar[0] == 1) {
      entry->avatar = avatar[1];
    }
  }
  return entry;
}

bool jewelColorForProfile(const String &profileId, uint32_t &rgb) {
  const JewelCacheEntry *entry = personalization(profileId);
  if (entry == nullptr) return false;
  rgb = entry->rgb;
  return entry->set;
}

uint8_t avatarForProfile(const String &profileId) {
  const JewelCacheEntry *entry = personalization(profileId);
  return entry == nullptr ? 0 : entry->avatar;
}

bool saveAvatarForProfile(const String &profileId, uint8_t avatar) {
  if (luxuryStore == nullptr || !profileExists(profileId)) return false;
  const String key = profileKey('v', profileId);
  TurnHubStorage::Status status;
  if (avatar != 0) {
    const uint8_t record[2] = {1, avatar};
    status = luxuryStore->write(key.c_str(), record, sizeof(record));
  } else {
    status = luxuryStore->remove(key.c_str());
    if (status == TurnHubStorage::Status::NotFound) status = TurnHubStorage::Status::Ok;
  }
  if (JewelCacheEntry *entry = cachedJewel(profileId)) *entry = JewelCacheEntry{};
  return status == TurnHubStorage::Status::Ok;
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

void setLuxuryStore(TurnHubStorage::BlobStore *store) {
  luxuryStore = store;
  // A different card (or none) holds different Jewel colors and avatars.
  for (auto &entry : jewelCache) entry = JewelCacheEntry{};
  jewelCacheNext = 0;
}

bool luxuryStoreAvailable() { return luxuryStore != nullptr; }

bool loadStatsForProfile(const String &profileId, ProfileStats &stats, bool *detailed) {
  using TurnHubStorage::Status;
  stats = ProfileStats{};
  if (detailed) *detailed = false;
  if (!preferencesReady || !profileExists(profileId)) {
    return false;
  }
  if (statsStorage.begin(PREF_NAMESPACE) != Status::Ok) return false;

  // NVS: the core record (none for a fresh profile). Any other failure must
  // stop the completion callback from replacing an unreadable record with
  // zero totals.
  CoreStats core;
  const Status coreStatus = readCoreStats(statsStorage, profileKey(CORE_STATS_PREFIX, profileId).c_str(), core);
  if (coreStatus != Status::Ok && coreStatus != Status::NotFound) return false;

  if (luxuryStore != nullptr) {
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
  // The detail goes to the card when there is one; without it only the core
  // counts are kept.
  if (luxuryStore != nullptr) writeStoredStats(*luxuryStore, detailKey.c_str(), stats);
  return true;
}

bool loadModerationStatsForProfile(const String &profileId, ModerationStats &stats) {
  stats = ModerationStats{};
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
  return boundProfileIdForSeat(mac, slot);
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

bool releaseSeatBinding(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady && !begin()) return false;
  if (slot != 1 && slot != 2) return false;
  if (auto *binding = transientSeatFor(mac, slot, false)) {
    binding->used = false;
    binding->profileId = String();
  }
  return true;
}

bool resetTransientSeatBindings(const uint8_t mac[6]) {
  return releaseSeatBinding(mac, 1) && releaseSeatBinding(mac, 2);
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
  return setNameForProfile(profileId, name);
}

String storedPinHashForSeat(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady && !begin()) {
    return String();
  }
  const String profileId = profileIdForSeat(mac, slot);
  return validProfileId(profileId) ? storedPinHashForProfile(profileId) : String();
}

bool setPinHashForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    const String &hash) {
  const String profileId = profileIdForSeat(mac, slot);
  if (!validProfileId(profileId)) {
    return false;
  }
  return setPinHashForProfile(profileId, hash);
}

bool clearPinForSeat(const uint8_t mac[6], uint8_t slot) {
  const String profileId = profileIdForSeat(mac, slot);
  if (validProfileId(profileId)) {
    clearPinForProfile(profileId);
  }
  return true;
}

bool hasPinForSeat(const uint8_t mac[6], uint8_t slot) {
  return storedPinHashForSeat(mac, slot).length() == 64;
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

// The first Admin's profile ID ("acctadmin", 8 characters plus NUL), or ""
// when none was set yet. False only when the record can't be read.
bool primaryAdmin(String &id) {
  id = "";
  TurnHubStorage::NvsBlobStore store;
  if (store.begin("turnhub") != Status::Ok) return false;
  size_t n = 0;
  Status status = store.read("acctadmin", nullptr, 0, n);
  if (status == Status::NotFound) return true;
  if (status != Status::Ok || n != 9) return false;
  char stored[9] = {};
  status = store.read("acctadmin", stored, sizeof(stored), n);
  if (status != Status::Ok || n != 9 || stored[8] != 0 ||
      !TurnHubProfiles::profileExists(String(stored))) return false;
  id = String(stored);
  return true;
}

bool establishAdmin(const String &id) {
  String current;
  if (!primaryAdmin(current) || current.length() || !TurnHubProfiles::hasPinForProfile(id)) return false;
  Account account;
  if (!load(id, account)) return false;
  TurnHubStorage::NvsBlobStore store;
  return store.begin("turnhub") == Status::Ok && store.write("acctadmin", id.c_str(), 9) == Status::Ok;
}

}  // namespace TurnHubAccounts
