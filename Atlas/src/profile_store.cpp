#include "profile_store.h"

#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

namespace TurnHubProfiles {
namespace {

constexpr char PREF_NAMESPACE[] = "turnhub";
constexpr size_t PROFILE_ID_LENGTH = 8;

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

String profileKey(char prefix, const String &profileId) {
  String key;
  key.reserve(1 + PROFILE_ID_LENGTH);
  key += prefix;
  key += profileId;
  return key;
}

String makeProfileId() {
  char id[PROFILE_ID_LENGTH + 1];
  snprintf(id, sizeof(id), "%08lX", static_cast<unsigned long>(esp_random()));
  return String(id);
}

String ensureProfileId(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady || (slot != 1 && slot != 2)) {
    return String();
  }

  const String bindingKey = seatKey('b', mac, slot);
  String profileId = preferences.getString(bindingKey.c_str(), "");
  if (profileId.length() == PROFILE_ID_LENGTH) {
    return profileId;
  }

  // Eight hex characters are enough for a local Atlas profile identifier.
  // Check for an existing profile key so an unlikely random collision never
  // merges two people.
  for (uint8_t attempt = 0; attempt < 16; ++attempt) {
    profileId = makeProfileId();
    const String markerKey = profileKey('m', profileId);
    if (!preferences.isKey(markerKey.c_str())) {
      if (preferences.putUChar(markerKey.c_str(), 1) == 0 ||
          preferences.putString(bindingKey.c_str(), profileId) == 0) {
        return String();
      }
      return profileId;
    }
  }

  return String();
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

String profileIdForSeat(const uint8_t mac[6], uint8_t slot) {
  return ensureProfileId(mac, slot);
}

String nameForSeat(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady) {
    return String();
  }

  const String profileId = ensureProfileId(mac, slot);
  if (profileId.length() == PROFILE_ID_LENGTH) {
    const String key = profileKey('n', profileId);
    const String stored = preferences.getString(key.c_str(), "");
    if (stored.length() > 0) {
      return stored;
    }
  }

  // Read the old hardware-seat key so existing names survive the migration.
  return preferences.getString(seatKey('n', mac, slot).c_str(), "");
}

bool setNameForSeat(const uint8_t mac[6], uint8_t slot, const String &name) {
  if (!preferencesReady) {
    return false;
  }

  const String profileId = ensureProfileId(mac, slot);
  if (profileId.length() != PROFILE_ID_LENGTH) {
    return false;
  }

  const String key = profileKey('n', profileId);
  if (name.length() == 0) {
    preferences.remove(key.c_str());
    preferences.remove(seatKey('n', mac, slot).c_str());
    return true;
  }

  const bool ok = preferences.putString(key.c_str(), name) > 0;
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
  if (!preferencesReady) {
    return String();
  }

  const String profileId = ensureProfileId(mac, slot);
  if (profileId.length() == PROFILE_ID_LENGTH) {
    const String key = profileKey('p', profileId);
    const String stored = preferences.getString(key.c_str(), "");
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
  if (!preferencesReady || hash.length() != 64) {
    return false;
  }

  const String profileId = ensureProfileId(mac, slot);
  if (profileId.length() != PROFILE_ID_LENGTH) {
    return false;
  }

  const bool ok = preferences.putString(profileKey('p', profileId).c_str(), hash) > 0;
  if (ok) {
    preferences.remove(seatKey('p', mac, slot).c_str());
  }
  return ok;
}

bool clearPinForSeat(const uint8_t mac[6], uint8_t slot) {
  if (!preferencesReady) {
    return false;
  }

  const String profileId = ensureProfileId(mac, slot);
  if (profileId.length() == PROFILE_ID_LENGTH) {
    preferences.remove(profileKey('p', profileId).c_str());
  }
  preferences.remove(seatKey('p', mac, slot).c_str());
  return true;
}

bool hasPinForSeat(const uint8_t mac[6], uint8_t slot) {
  return storedPinHashForSeat(mac, slot, nullptr).length() == 64;
}

String deviceName(const uint8_t mac[6]) {
  if (!preferencesReady) {
    return String();
  }
  return preferences.getString(seatKey('d', mac, 1).substring(0, 13).c_str(), "");
}

bool setDeviceName(const uint8_t mac[6], const String &name) {
  if (!preferencesReady) {
    return false;
  }

  char key[14];
  snprintf(
      key,
      sizeof(key),
      "d%02X%02X%02X%02X%02X%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (name.length() == 0) {
    preferences.remove(key);
    return true;
  }
  return preferences.putString(key, name) > 0;
}

bool loadStatsForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    ProfileStats &stats) {
  stats = ProfileStats{};
  if (!preferencesReady) {
    return false;
  }

  const String profileId = ensureProfileId(mac, slot);
  if (profileId.length() != PROFILE_ID_LENGTH) {
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

bool saveStatsForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    const ProfileStats &stats) {
  if (!preferencesReady) {
    return false;
  }

  const String profileId = ensureProfileId(mac, slot);
  if (profileId.length() != PROFILE_ID_LENGTH) {
    return false;
  }

  ProfileStats stored = stats;
  stored.schemaVersion = STATS_SCHEMA_VERSION;
  const String key = profileKey('s', profileId);
  return preferences.putBytes(key.c_str(), &stored, sizeof(stored)) == sizeof(stored);
}

}  // namespace TurnHubProfiles
