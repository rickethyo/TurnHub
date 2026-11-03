#pragma once

// Home Wi-Fi as Atlas's primary network (Ricky, 2026-11-03; plan in
// HOME_WIFI.md). Off until an Admin saves a home network, which turns it on.
// Once on, Atlas joins the home network soon after start-up and stays on it,
// rejoining by itself when the router drops it. Its own TurnHub-Atlas hotspot
// stays up the whole time as the fallback for phones.
//
// One radio serves the hotspot, ESP-NOW and the home network, so all three
// share a channel: while Atlas is on the home network that is the router's
// channel, and the hotspot and ESP-NOW move with it (Sigils find the new
// channel by searching, channel_search.h). The saved table channel is used
// only until Atlas first joins. A router that changes channel mid-match moves
// the table too (accepted 2026-11-03); phones carry on through offline play.
//
// This header is the pure part, host-tested: when to (re)join, when a release
// feed check is due and how the feed is read. home_wifi_link.cpp (firmware
// only) does the radio work; the settings live in NVS under
// AtlasConfig::WIFI_PREF_NAMESPACE.

#include <stdint.h>
#include <string.h>

#include "update_notice.h"

namespace TurnHub {

// Release feed checks while on the home network: the first a little after
// start-up, then every few hours; a failed one tries again sooner.
constexpr uint32_t HOME_WIFI_FIRST_CHECK_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t HOME_WIFI_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t HOME_WIFI_RETRY_MS = 30UL * 60UL * 1000UL;
// How long one join attempt may take before it counts as failed.
constexpr uint32_t HOME_WIFI_JOIN_TIMEOUT_MS = 15000;
constexpr uint32_t HOME_WIFI_FETCH_TIMEOUT_MS = 10000;
constexpr size_t HOME_WIFI_SSID_MAX_LENGTH = 32;
constexpr size_t HOME_WIFI_PASSWORD_MAX_LENGTH = 63;

// Joining: the first try a little after start-up (so the hotspot and Sigils
// come up first), then after a failure 30 s, doubling to at most 5 min. A
// dropped connection retries after the start-up delay again.
constexpr uint32_t HOME_WIFI_START_JOIN_MS = 10000;
constexpr uint32_t HOME_WIFI_REJOIN_MIN_MS = 30000;
constexpr uint32_t HOME_WIFI_REJOIN_MAX_MS = 5UL * 60UL * 1000UL;

// The last release-feed check's outcome, for the screen, app and serial log.
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

// When the next release feed check is due. RAM only: a restart starts over.
class HomeWifiSchedule {
 public:
  // Ask for a check as soon as the table allows one (Check now, or the
  // setting just turned on).
  void requestNow() { requested_ = true; }

  // A check should start now: on, a network stored, the table idle, and
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

// When Atlas next tries to join the home network. RAM only.
class HomeWifiRejoin {
 public:
  // Join as soon as possible (network saved, setting turned on).
  void requestNow() { requested_ = true; }
  void joined() { requested_ = false; failures_ = 0; }
  void failed(uint32_t nowMs) {
    requested_ = false;
    tried_ = true;
    lastMs_ = nowMs;
    if (failures_ < 16) ++failures_;
  }
  // The router dropped an established connection.
  void lost(uint32_t nowMs) {
    requested_ = false;
    tried_ = true;
    lastMs_ = nowMs;
    failures_ = 0;
  }

  uint32_t waitMs() const {
    if (failures_ == 0) return HOME_WIFI_START_JOIN_MS;
    uint32_t wait = HOME_WIFI_REJOIN_MIN_MS;
    for (uint8_t i = 1; i < failures_ && wait < HOME_WIFI_REJOIN_MAX_MS; ++i) wait *= 2;
    return wait < HOME_WIFI_REJOIN_MAX_MS ? wait : HOME_WIFI_REJOIN_MAX_MS;
  }

  bool due(uint32_t nowMs, bool enabled, bool configured) const {
    if (!enabled || !configured) return false;
    if (requested_) return true;
    return static_cast<uint32_t>(nowMs - (tried_ ? lastMs_ : 0)) >= waitMs();
  }

  uint8_t failures() const { return failures_; }

 private:
  bool requested_ = false;
  bool tried_ = false;
  uint8_t failures_ = 0;
  uint32_t lastMs_ = 0;
};

// How a join may search. A full scan hops the radio across channels, which
// briefly takes the hotspot and ESP-NOW with it; during a match Atlas only
// looks on the channel it is already on (where Sigils are).
enum class HomeWifiJoinScan : uint8_t { AllChannels, CurrentChannelOnly };

inline HomeWifiJoinScan homeWifiJoinScan(bool tableIdle) {
  return tableIdle ? HomeWifiJoinScan::AllChannels : HomeWifiJoinScan::CurrentChannelOnly;
}

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
// On the home network now.
bool homeWifiConnected();
// Atlas's address on the home network ("" when not connected).
const char *homeWifiAddress();
// The home network's channel when Atlas last joined it, or 0.
uint8_t homeWifiNetworkChannel();
// Turns the setting on or off (saved). Turning it on joins at once; off
// leaves the home network and puts the radio back on the table channel.
// False if it could not be saved, or on with no network stored.
bool setHomeWifiEnabled(bool enabled);
// Saves the home network and turns home Wi-Fi on (an empty password keeps
// the stored one when the name is unchanged). False if invalid or not saved.
bool storeHomeWifiNetwork(const char *ssid, const char *password);
// Forgets the network and turns the setting off.
bool forgetHomeWifiNetwork();
// The stored network name ("" when none). Never the password.
const char *homeWifiSsid();
void requestHomeWifiCheck();
TurnHub::HomeWifiResult homeWifiResult();
// Milliseconds since the last finished feed check, or -1 before the first.
int32_t homeWifiLastCheckAgeMs(uint32_t nowMs);
void serviceHomeWifi(uint32_t nowMs);

}  // namespace TurnHubAtlas
