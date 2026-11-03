#include "home_wifi.h"

// The radio side of home Wi-Fi (home_wifi.h).
// Firmware only: the host tests stub this file in test_globals.cpp.

// Project headers first: HTTPClient pulls in mbedTLS's TAG_BYTES macro,
// which would clash with secure_link.h's constant of that name.
#include "atlas_app.h"
#include "config.h"
#include "home_tls.h"
#include "optional_preferences.h"
#include "serial_log.h"
#include "sigil_update_service.h"
#include "wifi_password_store.h"
#include "wall_clock.h"
#include <sys/time.h>
#include <esp_sntp.h>

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using TurnHub::serialLog;

namespace TurnHubAtlas {
namespace {

// The feed the app reads too (FirmwareReleases.kt DEFAULT_BASE_URL).
constexpr char FEED_URL[] = "https://github.com/rickethyo/TurnHub/releases/latest/download/turnhub-firmware.json";
constexpr size_t FEED_MAX_BYTES = 8192;

// Probing: during a match, an async scan of the current channel only, so
// joining never hops the radio away from the Sigils.
enum class Phase : uint8_t { Idle, Probing, Joining, Connected };

bool loaded = false;
bool enabled = false;
char ssid[TurnHub::HOME_WIFI_SSID_MAX_LENGTH + 1] = "";
char password[TurnHub::HOME_WIFI_PASSWORD_MAX_LENGTH + 1] = "";
TurnHub::HomeWifiRejoin rejoin;
TurnHub::HomeWifiSchedule schedule;
TurnHub::HomeWifiResult result = TurnHub::HomeWifiResult::None;
Phase phase = Phase::Idle;
uint32_t phaseAtMs = 0;
uint8_t tableChannel = AtlasConfig::WIFI_CHANNEL;
// The radio's channel before a join, restored if the join fails.
uint8_t channelBeforeJoin = 0;
// The home network's channel when Atlas last joined it (0 before).
uint8_t networkChannel = 0;
char address[16] = "";
bool mdnsStarted = false;
bool mdnsPinned = false;

// The release feed fetch runs in its own task while Atlas stays connected.
// fetchOk and fetched are read by loop() once fetchDone is set.
bool fetching = false;
std::atomic<bool> fetchDone{false};
bool fetchOk = false;
TurnHub::LatestFirmware fetched;

void load() {
  if (loaded) return;
  loaded = true;
  tableChannel = TurnHub::readTableChannel();
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(AtlasConfig::WIFI_PREF_NAMESPACE, true)) return;
  enabled = prefs.getString(AtlasConfig::HOME_WIFI_PREF_ON, "0") == "1";
  snprintf(ssid, sizeof(ssid), "%s", prefs.getString(AtlasConfig::HOME_WIFI_PREF_SSID, "").c_str());
  snprintf(password, sizeof(password), "%s", prefs.getString(AtlasConfig::HOME_WIFI_PREF_PASS, "").c_str());
  prefs.end();
}

bool save(const char *key, const char *value) {
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(AtlasConfig::WIFI_PREF_NAMESPACE, false)) return false;
  const bool ok = value[0] == '\0' ? prefs.remove(key) : prefs.putString(key, value) > 0;
  prefs.end();
  return ok;
}

// No match on any table and no firmware being written.
bool tableIdle() {
  for (const GameTable &t : tables) {
    if (t.hubState == HubState::Running || t.hubState == HubState::Paused) return false;
  }
  return !sigilUpdatesBusy() && !ota.inProgress();
}

uint8_t radioChannel() {
  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary, &second);
  return primary;
}

void fetchTask(void *) {
  WiFiClientSecure client;
  // The feed is unsigned anyway (FIRMWARE_UPDATES.md): a forged one can only
  // light the update notice, as a forged POST /api/updates/latest can, and
  // packages are still signature-checked before they install.
  client.setInsecure();
  // Bounded at every step: WiFiClientSecure's handshake would otherwise wait
  // up to 120 s (seen on the board 2026-11-03 while the screen stalled).
  client.setHandshakeTimeout(TurnHub::HOME_WIFI_FETCH_TIMEOUT_MS / 1000);
  serialLog.printf("ATLAS|HOME_WIFI|FEED|START|internal=%u|largest=%u\n",
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(TurnHub::HOME_WIFI_FETCH_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  bool ok = false;
  if (http.begin(client, FEED_URL)) {
    const int code = http.GET();
    if (code == 200 && http.getSize() <= static_cast<int>(FEED_MAX_BYTES)) {
      const String body = http.getString();
      ok = body.length() <= FEED_MAX_BYTES && TurnHub::parseReleaseFeed(body.c_str(), fetched);
    }
    serialLog.printf("ATLAS|HOME_WIFI|FEED|%d|%s\n", code, code < 0 ? http.errorToString(code).c_str() : "");
    http.end();
  }
  fetchOk = ok;
  fetchDone = true;
  vTaskDelete(nullptr);
}

void startFetch(uint32_t nowMs) {
  fetchDone = false;
  result = TurnHub::HomeWifiResult::Checking;
  // Core 0, away from loop() and the screen on core 1.
  if (xTaskCreatePinnedToCore(fetchTask, "home-wifi", 8192, nullptr, 1, nullptr, 0) == pdPASS) {
    fetching = true;
    return;
  }
  result = TurnHub::HomeWifiResult::FeedFailed;
  schedule.finished(nowMs, false);
}

// Turned off, forgotten or another network saved: leave the home network and
// put the hotspot and ESP-NOW back on the saved table channel.
void leave() {
  if (phase == Phase::Probing) WiFi.scanDelete();
  WiFi.disconnect(false, false);
  esp_wifi_set_channel(tableChannel, WIFI_SECOND_CHAN_NONE);
  phase = Phase::Idle;
  address[0] = '\0';
}

// A join attempt that found nothing: stay on the channel the radio was on.
void joinFailed(uint32_t nowMs, const char *why) {
  WiFi.disconnect(false, false);
  if (channelBeforeJoin) esp_wifi_set_channel(channelBeforeJoin, WIFI_SECOND_CHAN_NONE);
  phase = Phase::Idle;
  rejoin.failed(nowMs);
  if (result == TurnHub::HomeWifiResult::None || result == TurnHub::HomeWifiResult::Checking)
    result = TurnHub::HomeWifiResult::NoNetwork;
  serialLog.printf("ATLAS|HOME_WIFI|JOIN_FAILED|%s|retry_ms=%lu\n", why,
      static_cast<unsigned long>(rejoin.waitMs()));
}

void beginJoin(uint32_t nowMs, uint8_t channel) {
  // Kept out of the SDK's own flash store and never retried by it: rejoin
  // decides when the radio may search (home_wifi.h).
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.begin(ssid, password[0] ? password : nullptr, channel);
  phase = Phase::Joining;
  phaseAtMs = nowMs;
  serialLog.printf("ATLAS|HOME_WIFI|JOINING|hint=%u\n", static_cast<unsigned>(channel));
}

void startMdns() {
  if (mdnsStarted) return;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  const String host = String("atlas-") + mac;
  if (!MDNS.begin(host.c_str())) {
    serialLog.println("ATLAS|HOME_WIFI|MDNS|ERROR");
    return;
  }
  // HTTPS with a pinned key (home_tls.h); `fp` follows once the key exists.
  // The app uses the home network from HOME_WIFI.md slice 3 on.
  MDNS.addService("turnhub-atlas", "tcp", TurnHub::HOME_TLS_PORT);
  MDNS.addServiceTxt("turnhub-atlas", "tcp", "id", (String("THA-") + mac).c_str());
  MDNS.addServiceTxt("turnhub-atlas", "tcp", "api", "1");
  MDNS.addServiceTxt("turnhub-atlas", "tcp", "tls", "1");
  mdnsStarted = true;
  serialLog.print("ATLAS|HOME_WIFI|MDNS|");
  serialLog.println(host);
}

void joined() {
  configTime(0, 0, "time.google.com", "pool.ntp.org");
  networkChannel = static_cast<uint8_t>(WiFi.channel());
  snprintf(address, sizeof(address), "%s", WiFi.localIP().toString().c_str());
  phase = Phase::Connected;
  rejoin.joined();
  // The hotspot and ESP-NOW now run on the router's channel.
  serialLog.printf("ATLAS|HOME_WIFI|JOINED|channel=%u|table=%u\n",
      static_cast<unsigned>(networkChannel), static_cast<unsigned>(tableChannel));
  startHomeTls();
  startMdns();
}

// The key's pin in mDNS, once the TLS server has made or loaded it.
void publishPin() {
  if (!mdnsStarted || mdnsPinned || !homeTlsRunning()) return;
  MDNS.addServiceTxt("turnhub-atlas", "tcp", "fp", homeTlsPin());
  mdnsPinned = true;
}

// Bench, temporarily (owner, 2026-11-03): "disable the atlas hotspot and move
// all communication to the home wifi". Phones learn Atlas's key over the
// hotspot once, so it stays up for 2 min after Atlas joins the home network,
// then goes off (ESP-NOW stays on the station interface). With no home
// network for 1 min it comes back, so a lost router never strands the table.
#ifndef TURNHUB_HOTSPOT_OFF_ON_HOME
#define TURNHUB_HOTSPOT_OFF_ON_HOME 1
#endif
constexpr uint32_t HOTSPOT_OFF_AFTER_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t HOTSPOT_BACK_AFTER_MS = 60UL * 1000UL;
bool hotspotUp = true;
uint32_t homeUpSinceMs = 0, homeDownSinceMs = 0;

void serviceHotspot(uint32_t nowMs) {
#if TURNHUB_HOTSPOT_OFF_ON_HOME
  const bool home = phase == Phase::Connected;
  if (home) {
    homeDownSinceMs = 0;
    if (!homeUpSinceMs) homeUpSinceMs = nowMs | 1;
  } else {
    homeUpSinceMs = 0;
    if (!homeDownSinceMs) homeDownSinceMs = nowMs | 1;
  }
  if (hotspotUp && home && nowMs - homeUpSinceMs >= HOTSPOT_OFF_AFTER_MS) {
    WiFi.softAPdisconnect(true);
    hotspotUp = false;
    serialLog.printf("ATLAS|WIFI_AP|OFF|home_only|channel=%u\n", static_cast<unsigned>(radioChannel()));
  } else if (!hotspotUp && !home && nowMs - homeDownSinceMs >= HOTSPOT_BACK_AFTER_MS) {
    const String stored = TurnHub::readStoredWifiPassword();
    const String password = TurnHub::validWifiPassword(stored) ? stored : String(AtlasConfig::WIFI_DEFAULT_PASSWORD);
    WiFi.mode(WIFI_AP_STA);
    hotspotUp = WiFi.softAP(AtlasConfig::WIFI_SSID, password.c_str(), radioChannel(), false, 8);
    WiFi.setSleep(false);
    serialLog.printf("ATLAS|WIFI_AP|%s|no_home_wifi|channel=%u\n", hotspotUp ? "BACK" : "ERROR",
        static_cast<unsigned>(radioChannel()));
  }
#else
  (void)nowMs;
#endif
}

}  // namespace

bool homeWifiEnabled() { load(); return enabled; }
bool homeWifiConfigured() { load(); return ssid[0] != '\0'; }
bool homeWifiConnected() { return phase == Phase::Connected; }
const char *homeWifiAddress() { return phase == Phase::Connected ? address : ""; }
bool atlasHotspotOn() { return hotspotUp; }
uint8_t homeWifiNetworkChannel() { return networkChannel; }
const char *homeWifiSsid() { load(); return ssid; }
TurnHub::HomeWifiResult homeWifiResult() { return result; }

int32_t homeWifiLastCheckAgeMs(uint32_t nowMs) {
  return schedule.checked() ? static_cast<int32_t>(nowMs - schedule.lastMs()) : -1;
}

bool setHomeWifiEnabled(bool on) {
  load();
  if (on && ssid[0] == '\0') return false;
  if (!save(AtlasConfig::HOME_WIFI_PREF_ON, on ? "1" : "0")) return false;
  enabled = on;
  if (on) {
    rejoin.requestNow();
    schedule.requestNow();
  } else if (phase != Phase::Idle) {
    leave();
  }
  serialLog.println(on ? "ATLAS|HOME_WIFI|ON" : "ATLAS|HOME_WIFI|OFF");
  return true;
}

bool storeHomeWifiNetwork(const char *newSsid, const char *newPassword) {
  load();
  const size_t ssidLength = newSsid ? strlen(newSsid) : 0;
  const size_t passLength = newPassword ? strlen(newPassword) : 0;
  if (ssidLength == 0 || ssidLength > TurnHub::HOME_WIFI_SSID_MAX_LENGTH) return false;
  // Open networks have no password; WPA2 needs 8 to 63 characters.
  if (passLength > TurnHub::HOME_WIFI_PASSWORD_MAX_LENGTH || (passLength > 0 && passLength < 8)) return false;
  const bool sameNetwork = strcmp(ssid, newSsid) == 0;
  const char *keep = passLength == 0 && sameNetwork ? password : newPassword;
  char pass[sizeof(password)];
  snprintf(pass, sizeof(pass), "%s", keep ? keep : "");
  // Saving a network turns home Wi-Fi on: that is what it was saved for.
  if (!save(AtlasConfig::HOME_WIFI_PREF_SSID, newSsid) || !save(AtlasConfig::HOME_WIFI_PREF_PASS, pass) ||
      !save(AtlasConfig::HOME_WIFI_PREF_ON, "1")) return false;
  snprintf(ssid, sizeof(ssid), "%s", newSsid);
  snprintf(password, sizeof(password), "%s", pass);
  enabled = true;
  result = TurnHub::HomeWifiResult::None;
  networkChannel = 0;
  if (phase != Phase::Idle) leave();
  rejoin = TurnHub::HomeWifiRejoin();
  rejoin.requestNow();
  schedule.requestNow();
  serialLog.println("ATLAS|HOME_WIFI|NETWORK_SAVED|ON");
  return true;
}

bool forgetHomeWifiNetwork() {
  load();
  const bool ok = save(AtlasConfig::HOME_WIFI_PREF_ON, "0") && save(AtlasConfig::HOME_WIFI_PREF_SSID, "") &&
      save(AtlasConfig::HOME_WIFI_PREF_PASS, "");
  if (ok) {
    if (phase != Phase::Idle) leave();
    enabled = false;
    networkChannel = 0;
    ssid[0] = password[0] = '\0';
    result = TurnHub::HomeWifiResult::None;
    serialLog.println("ATLAS|HOME_WIFI|FORGOTTEN");
  }
  return ok;
}

void requestHomeWifiCheck() {
  schedule.requestNow();
  if (phase == Phase::Idle) rejoin.requestNow();
}

void serviceHomeWifi(uint32_t nowMs) {
  serviceHotspot(nowMs);
  static uint32_t lastClockCheck = 0;
  if (WiFi.status() == WL_CONNECTED && nowMs - lastClockCheck >= 1000) {
    lastClockCheck = nowMs;
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      timeval utc{};
      gettimeofday(&utc, nullptr);
      if (utc.tv_sec >= 1577836800LL)
        TurnHubClock::clock.observe(static_cast<uint64_t>(utc.tv_sec) * 1000 + utc.tv_usec / 1000,
            nowMs, TurnHubClock::Source::Internet);
    }
  }
  TurnHubClock::clock.now(nowMs);

  // A finished feed check, whatever the connection did meanwhile.
  if (fetching && fetchDone) {
    fetching = false;
    if (fetchOk) noteLatestFirmware(fetched);
    result = fetchOk ? TurnHub::HomeWifiResult::Checked : TurnHub::HomeWifiResult::FeedFailed;
    schedule.finished(nowMs, fetchOk);
    serialLog.print("ATLAS|HOME_WIFI|CHECKED|");
    serialLog.println(TurnHub::homeWifiResultName(result));
  }

  load();
  switch (phase) {
    case Phase::Idle: {
      if (!rejoin.due(nowMs, enabled, ssid[0] != '\0')) return;
      channelBeforeJoin = radioChannel();
      if (result != TurnHub::HomeWifiResult::Checked) result = TurnHub::HomeWifiResult::Checking;
      if (TurnHub::homeWifiJoinScan(tableIdle()) == TurnHub::HomeWifiJoinScan::AllChannels) {
        // The last known channel first, then the others.
        beginJoin(nowMs, networkChannel);
        return;
      }
      // A match is running: look for the router on this channel only.
      WiFi.scanNetworks(true, false, false, 120, channelBeforeJoin, ssid);
      phase = Phase::Probing;
      phaseAtMs = nowMs;
      return;
    }
    case Phase::Probing: {
      const int16_t found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING) {
        if (nowMs - phaseAtMs < 3000) return;
        WiFi.scanDelete();
        joinFailed(nowMs, "probe_timeout");
        return;
      }
      bool here = false;
      for (int16_t i = 0; i < found; ++i) {
        if (WiFi.SSID(i) == ssid && WiFi.channel(i) == channelBeforeJoin) here = true;
      }
      WiFi.scanDelete();
      if (here) beginJoin(nowMs, channelBeforeJoin);
      else joinFailed(nowMs, "not_on_channel");
      return;
    }
    case Phase::Joining:
      if (WiFi.status() == WL_CONNECTED) { joined(); return; }
      if (nowMs - phaseAtMs >= TurnHub::HOME_WIFI_JOIN_TIMEOUT_MS) joinFailed(nowMs, "timeout");
      return;
    case Phase::Connected: {
      if (!enabled || ssid[0] == '\0') { leave(); return; }
      if (WiFi.status() != WL_CONNECTED) {
        // Stay on this channel (the Sigils are here) and rejoin by itself.
        phase = Phase::Idle;
        address[0] = '\0';
        rejoin.lost(nowMs);
        serialLog.println("ATLAS|HOME_WIFI|CONNECTION_LOST");
        return;
      }
      const uint8_t channel = static_cast<uint8_t>(WiFi.channel());
      if (channel != networkChannel) {
        // The router moved: the hotspot and ESP-NOW moved with it, and Sigils
        // search for the new channel.
        serialLog.printf("ATLAS|HOME_WIFI|CHANNEL|%u->%u\n", static_cast<unsigned>(networkChannel),
            static_cast<unsigned>(channel));
        networkChannel = channel;
      }
      publishPin();
      if (!fetching && schedule.due(nowMs, enabled, true, tableIdle())) startFetch(nowMs);
      return;
    }
  }
}

}  // namespace TurnHubAtlas
