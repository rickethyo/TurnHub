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

}  // namespace TurnHub
