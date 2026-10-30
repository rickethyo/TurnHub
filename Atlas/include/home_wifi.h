#pragma once

// Optional home Wi-Fi (Ricky, 2026-10-30): off by default. When an Admin
// stores a home network and turns it on, Atlas checks the release feed itself
// instead of waiting for an app to report it (update_notice.h). Its own
// TurnHub-Atlas hotspot stays up throughout, so phones join it as before.
//
// One radio serves the hotspot, ESP-NOW and the home network, so all three
// share a channel. Atlas's is the table channel (protocol.h; 6 unless the
// owner picks another, which Sigils find by searching). A router on another
// channel would pull Atlas off it, so then Atlas joins the home network only
// for a short check-in with no game running (and no firmware update busy),
// and leaves it for the table channel again. A check-in may briefly drop a
// phone or Sigil in the lobby; each reconnects by itself. When the router
// shares the table channel, Atlas stays on the home network.
//
// This header is the pure part, host-tested: when a check-in is due and how
// the release feed is read. home_wifi_link.cpp (firmware only) does the radio
// work; the settings live in NVS under AtlasConfig::WIFI_PREF_NAMESPACE.

#include <stdint.h>
#include <string.h>

#include "update_notice.h"

namespace TurnHub {

// First check-in a little after start-up (so Sigils reconnect first), then
// every few hours; a failed one tries again sooner.
constexpr uint32_t HOME_WIFI_FIRST_CHECK_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t HOME_WIFI_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t HOME_WIFI_RETRY_MS = 30UL * 60UL * 1000UL;
// How long a check-in may hold the home network before giving up.
constexpr uint32_t HOME_WIFI_JOIN_TIMEOUT_MS = 15000;
constexpr uint32_t HOME_WIFI_FETCH_TIMEOUT_MS = 10000;
constexpr size_t HOME_WIFI_SSID_MAX_LENGTH = 32;
constexpr size_t HOME_WIFI_PASSWORD_MAX_LENGTH = 63;

// The last check-in's outcome, for the screen, portal and serial log.
enum class HomeWifiResult : uint8_t { None, Checking, Checked, NoNetwork, FeedFailed };

inline const char *homeWifiResultName(HomeWifiResult result) {
  switch (result) {
    case HomeWifiResult::Checking: return "checking";
    case HomeWifiResult::Checked: return "checked";
    case HomeWifiResult::NoNetwork: return "noNetwork";
    case HomeWifiResult::FeedFailed: return "feedFailed";
    default: return "none";
  }
}

// When the next check-in is due. RAM only: a restart starts over.
class HomeWifiSchedule {
 public:
  // Ask for a check-in as soon as the table allows one (Check now, or the
  // setting just turned on).
  void requestNow() { requested_ = true; }

  // A check-in should start now: on, a network stored, the table idle, and
  // either asked for or its time has come.
  bool due(uint32_t nowMs, bool enabled, bool configured, bool tableIdle) const {
    if (!enabled || !configured || !tableIdle) return false;
    if (requested_) return true;
    const uint32_t wait = !checked_ ? HOME_WIFI_FIRST_CHECK_MS : lastOk_ ? HOME_WIFI_CHECK_INTERVAL_MS : HOME_WIFI_RETRY_MS;
    return static_cast<uint32_t>(nowMs - (checked_ ? lastMs_ : 0)) >= wait;
  }

  void finished(uint32_t nowMs, bool ok) {
    requested_ = false;
    checked_ = true;
    lastOk_ = ok;
    lastMs_ = nowMs;
  }

  bool checked() const { return checked_; }
  uint32_t lastMs() const { return lastMs_; }

 private:
  bool requested_ = false;
  bool checked_ = false;
  bool lastOk_ = false;
  uint32_t lastMs_ = 0;
};

namespace HomeWifiDetail {
// The string value of `"key":"..."` between `from` and `end`, copied to out.
inline bool stringField(const char *from, const char *end, const char *key, char *out, size_t size) {
  const size_t keyLength = strlen(key);
  for (const char *p = from; p + keyLength + 2 < end; ++p) {
    if (*p != '"' || strncmp(p + 1, key, keyLength) != 0 || p[keyLength + 1] != '"') continue;
    const char *q = p + keyLength + 2;
    while (q < end && (*q == ' ' || *q == ':')) ++q;
    if (q >= end || *q != '"') return false;
    ++q;
    size_t n = 0;
    while (q < end && *q != '"' && n + 1 < size) out[n++] = *q++;
    out[n] = '\0';
    return q < end && *q == '"';
  }
  return false;
}
}  // namespace HomeWifiDetail

// Reads the newest Atlas and Sigil versions from turnhub-firmware.json
// (FIRMWARE_UPDATES.md, "Release feed"), the same file the app reads. Each
// package is a flat object; unknown products are ignored. False unless the
// schema is 1 and at least one product's version parses.
inline bool parseReleaseFeed(const char *json, LatestFirmware &out) {
  if (json == nullptr) return false;
  const char *schema = strstr(json, "\"schema\"");
  if (schema == nullptr) return false;
  schema += 8;
  while (*schema == ' ' || *schema == ':') ++schema;
  if (schema[0] != '1' || (schema[1] >= '0' && schema[1] <= '9')) return false;
  const char *packages = strstr(json, "\"packages\"");
  if (packages == nullptr) return false;
  LatestFirmware found;
  for (const char *open = strchr(packages, '{'); open != nullptr; open = strchr(open + 1, '{')) {
    const char *close = strchr(open, '}');
    if (close == nullptr) break;
    char product[16], version[24];
    if (HomeWifiDetail::stringField(open, close, "product", product, sizeof(product)) &&
        HomeWifiDetail::stringField(open, close, "version", version, sizeof(version))) {
      FirmwareRelease release;
      if (parseFirmwareRelease(version, release)) {
        if (strcmp(product, "atlas") == 0) found.atlas = release;
        if (strcmp(product, "sigil") == 0) found.sigil = release;
      }
    }
    open = close;
  }
  if (!found.atlas.known && !found.sigil.known) return false;
  out = found;
  return true;
}

}  // namespace TurnHub

namespace TurnHubAtlas {

// home_wifi_link.cpp (firmware); test_globals.cpp stubs them for host tests.
// Whether the setting is on and a home network is stored.
bool homeWifiEnabled();
bool homeWifiConfigured();
// Staying on the home network: it shares the table channel.
bool homeWifiConnected();
// The home network's channel when Atlas last joined it, or 0.
uint8_t homeWifiNetworkChannel();
// Turns the setting on or off (saved). Turning it on asks for a check-in.
// False if it could not be saved, or on with no network stored.
bool setHomeWifiEnabled(bool enabled);
// Saves the home network (an empty password keeps the stored one when the
// name is unchanged). False if invalid or not saved.
bool storeHomeWifiNetwork(const char *ssid, const char *password);
// Forgets the network and turns the setting off.
bool forgetHomeWifiNetwork();
// The stored network name ("" when none). Never the password.
const char *homeWifiSsid();
void requestHomeWifiCheck();
TurnHub::HomeWifiResult homeWifiResult();
// Milliseconds since the last finished check-in, or -1 before the first.
int32_t homeWifiLastCheckAgeMs(uint32_t nowMs);
void serviceHomeWifi(uint32_t nowMs);

}  // namespace TurnHubAtlas
