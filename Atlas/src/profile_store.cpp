#include "profile_store.h"

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
bool preferencesReady = false;

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

String ensureProfileId(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady || (slot != 1 && slot != 2)) {
    return String();
  }

  const String bindingKey = seatKey('b', mac, slot);
  String profileId = preferences.getString(bindingKey.c_str(), "");
  if (validProfileId(profileId) && profileExists(profileId)) {
    return profileId;
  }

  profileId = createProfile();
  if (!validProfileId(profileId)) {
    return String();
  }
  if (preferences.putString(bindingKey.c_str(), profileId) == 0) {
    return String();
  }
  return profileId;
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
      memcpy(ids[count++], info.key + 1, PROFILE_ID_LENGTH + 1);
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
  return preferences.putString(profileKey('p', profileId).c_str(), hash) > 0;
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

bool loadStatsForProfile(const String &profileId, ProfileStats &stats) {
  stats = ProfileStats{};
  if (!preferencesReady || !profileExists(profileId)) {
    return false;
  }

  const String key = profileKey('s', profileId);
  if (statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  const auto status = readStoredStats(statsStorage, key.c_str(), stats);
  // A fresh profile has no statistics. All other failures must stop the
  // completion callback from replacing an unreadable record with zero totals.
  return status == TurnHubStorage::Status::Ok ||
      status == TurnHubStorage::Status::NotFound;
}

bool saveStatsForProfile(const String &profileId, const ProfileStats &stats) {
  if (!preferencesReady || !profileExists(profileId)) {
    return false;
  }

  const String key = profileKey('s', profileId);
  if (statsStorage.begin(PREF_NAMESPACE) != TurnHubStorage::Status::Ok) return false;
  return writeStoredStats(statsStorage, key.c_str(), stats) == TurnHubStorage::Status::Ok;
}

String profileIdForSeat(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady && !begin()) {
    return String();
  }
  const String profileId = ensureProfileId(mac, slot);
  migrateLegacyName(mac, slot, profileId);
  return profileId;
}

bool bindSeatToProfile(
    const uint8_t mac[6],
    uint8_t slot,
    const String &profileId) {
  if (!preferencesReady || (slot != 1 && slot != 2) || !profileExists(profileId)) {
    return false;
  }
  return preferences.putString(seatKey('b', mac, slot).c_str(), profileId) > 0;
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
