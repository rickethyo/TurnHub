#include "profile_store.h"

#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

namespace TurnHubProfiles {
namespace {

constexpr char PREF_NAMESPACE[] = "turnhub";

Preferences preferences;
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
  if (profileId.length() != PROFILE_ID_LENGTH) {
    return false;
  }
  for (size_t i = 0; i < profileId.length(); ++i) {
    const char c = profileId[i];
    const bool hex =
        (c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f');
    if (!hex) {
      return false;
    }
  }
  return true;
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

bool validStoredStats(const ProfileStats &stats) {
  return stats.schemaVersion == STATS_SCHEMA_VERSION;
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
  if (preferences.getBytesLength(key.c_str()) != sizeof(ProfileStats)) {
    return true;
  }

  ProfileStats stored{};
  if (preferences.getBytes(key.c_str(), &stored, sizeof(stored)) != sizeof(stored) ||
      !validStoredStats(stored)) {
    return true;
  }

  stats = stored;
  return true;
}

bool saveStatsForProfile(const String &profileId, const ProfileStats &stats) {
  if (!preferencesReady || !profileExists(profileId)) {
    return false;
  }

  ProfileStats stored = stats;
  stored.schemaVersion = STATS_SCHEMA_VERSION;
  const String key = profileKey('s', profileId);
  return preferences.putBytes(key.c_str(), &stored, sizeof(stored)) == sizeof(stored);
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
