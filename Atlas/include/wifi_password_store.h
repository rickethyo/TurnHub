#pragma once

// The owner-set Atlas AP password in NVS (AtlasConfig::WIFI_PREF_*). Read by
// startup and by the admin network endpoints; written only by the portal.

#include <Arduino.h>

#include "config.h"
#include "optional_preferences.h"

namespace TurnHub {

// The stored password, or an empty string when none is stored or NVS is
// unavailable. Callers fall back to AtlasConfig::WIFI_DEFAULT_PASSWORD.
inline String readStoredWifiPassword() {
  OptionalPreferences prefs;
  String password;
  if (prefs.begin(AtlasConfig::WIFI_PREF_NAMESPACE, true)) {
    password = prefs.getString(AtlasConfig::WIFI_PREF_KEY, "");
    prefs.end();
  }
  return password;
}

inline bool validWifiPassword(const String &password) {
  return password.length() >= AtlasConfig::WIFI_PASSWORD_MIN_LENGTH &&
      password.length() <= AtlasConfig::WIFI_PASSWORD_MAX_LENGTH;
}

// The table channel (protocol.h): the owner's choice, or the default.
// Used from the next start-up.
inline uint8_t readTableChannel() {
  OptionalPreferences prefs;
  long channel = AtlasConfig::WIFI_CHANNEL;
  if (prefs.begin(AtlasConfig::WIFI_PREF_NAMESPACE, true)) {
    const String stored = prefs.getString(AtlasConfig::TABLE_CHANNEL_PREF_KEY, "");
    if (stored.length() > 0) channel = stored.toInt();
    prefs.end();
  }
  return channel >= 0 && channel <= 255 && TurnHubProtocol::validTableChannel(static_cast<uint8_t>(channel))
      ? static_cast<uint8_t>(channel) : AtlasConfig::WIFI_CHANNEL;
}

}  // namespace TurnHub
