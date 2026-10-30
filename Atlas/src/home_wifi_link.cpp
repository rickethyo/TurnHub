#include "home_wifi.h"

// The radio side of the optional home Wi-Fi check-in (home_wifi.h).
// Firmware only: the host tests stub this file in test_globals.cpp.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "atlas_app.h"
#include "config.h"
#include "optional_preferences.h"
#include "serial_log.h"
#include "sigil_update_service.h"
#include "wifi_password_store.h"

namespace TurnHubAtlas {
namespace {

// The feed the app reads too (FirmwareReleases.kt DEFAULT_BASE_URL).
constexpr char FEED_URL[] = "https://github.com/rickethyo/TurnHub/releases/latest/download/turnhub-firmware.json";
constexpr size_t FEED_MAX_BYTES = 8192;

// Connected: the home network shares the table channel, so Atlas stays on it.
enum class Phase : uint8_t { Idle, Joining, Fetching, Connected };

bool loaded = false;
bool enabled = false;
char ssid[TurnHub::HOME_WIFI_SSID_MAX_LENGTH + 1] = "";
char password[TurnHub::HOME_WIFI_PASSWORD_MAX_LENGTH + 1] = "";
TurnHub::HomeWifiSchedule schedule;
TurnHub::HomeWifiResult result = TurnHub::HomeWifiResult::None;
Phase phase = Phase::Idle;
uint32_t phaseAtMs = 0;
uint8_t tableChannel = AtlasConfig::WIFI_CHANNEL;
// The home network's channel when Atlas last joined it (0 before).
uint8_t networkChannel = 0;

// Written by the fetch task, read by loop() once fetchDone is set.
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

// No match on any table and no firmware being written: a channel change
// can't cost a turn or a package.
bool tableIdle() {
  for (const GameTable &t : tables) {
    if (t.hubState == HubState::Running || t.hubState == HubState::Paused) return false;
  }
  return !sigilUpdatesBusy() && !ota.inProgress();
}

void fetchTask(void *) {
  WiFiClientSecure client;
  // The feed is unsigned anyway (FIRMWARE_UPDATES.md): a forged one can only
  // light the update notice, as a forged POST /api/updates/latest can, and
  // packages are still signature-checked before they install.
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(TurnHub::HOME_WIFI_FETCH_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  bool ok = false;
  if (http.begin(client, FEED_URL)) {
    const int code = http.GET();
    if (code == 200 && http.getSize() <= static_cast<int>(FEED_MAX_BYTES)) {
      const String body = http.getString();
      ok = body.length() <= FEED_MAX_BYTES && TurnHub::parseReleaseFeed(body.c_str(), fetched);
    }
    serialLog.printf("ATLAS|HOME_WIFI|FEED|%d\n", code);
    http.end();
  }
  fetchOk = ok;
  fetchDone = true;
  vTaskDelete(nullptr);
}

// Leaves the home network and puts the hotspot and ESP-NOW back on the
// Sigils' channel.
void leave() {
  WiFi.disconnect(false, false);
  esp_wifi_set_channel(tableChannel, WIFI_SECOND_CHAN_NONE);
  phase = Phase::Idle;
}

// A check-in ended: stay on a home network that shares the table channel,
// otherwise leave it.
void finish(uint32_t nowMs, TurnHub::HomeWifiResult outcome) {
  result = outcome;
  schedule.finished(nowMs, outcome == TurnHub::HomeWifiResult::Checked);
  const bool stay = WiFi.status() == WL_CONNECTED && networkChannel == tableChannel;
  if (stay) phase = Phase::Connected;
  else leave();
  serialLog.print("ATLAS|HOME_WIFI|DONE|");
  serialLog.print(TurnHub::homeWifiResultName(outcome));
  serialLog.println(stay ? "|STAYING" : "|LEFT");
}

bool startFetch() {
  fetchDone = false;
  return xTaskCreate(fetchTask, "home-wifi", 8192, nullptr, 1, nullptr) == pdPASS;
}

}  // namespace

bool homeWifiEnabled() { load(); return enabled; }
bool homeWifiConfigured() { load(); return ssid[0] != '\0'; }
bool homeWifiConnected() { return phase == Phase::Connected; }
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
  if (on) schedule.requestNow();
  else if (phase == Phase::Connected) leave();
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
  if (!save(AtlasConfig::HOME_WIFI_PREF_SSID, newSsid) || !save(AtlasConfig::HOME_WIFI_PREF_PASS, pass)) return false;
  snprintf(ssid, sizeof(ssid), "%s", newSsid);
  snprintf(password, sizeof(password), "%s", pass);
  result = TurnHub::HomeWifiResult::None;
  networkChannel = 0;
  if (phase == Phase::Connected) leave();
  if (enabled) schedule.requestNow();
  serialLog.println("ATLAS|HOME_WIFI|NETWORK_SAVED");
  return true;
}

bool forgetHomeWifiNetwork() {
  load();
  const bool ok = save(AtlasConfig::HOME_WIFI_PREF_ON, "0") && save(AtlasConfig::HOME_WIFI_PREF_SSID, "") &&
      save(AtlasConfig::HOME_WIFI_PREF_PASS, "");
  if (ok) {
    if (phase == Phase::Connected) leave();
    enabled = false;
    networkChannel = 0;
    ssid[0] = password[0] = '\0';
    result = TurnHub::HomeWifiResult::None;
    serialLog.println("ATLAS|HOME_WIFI|FORGOTTEN");
  }
  return ok;
}

void requestHomeWifiCheck() { schedule.requestNow(); }

void serviceHomeWifi(uint32_t nowMs) {
  load();
  switch (phase) {
    case Phase::Idle:
      if (!schedule.due(nowMs, enabled, ssid[0] != '\0', tableIdle())) return;
      // Kept out of the SDK's own flash store, and never retried by it: a
      // lost home network must not keep the radio scanning other channels.
      WiFi.persistent(false);
      WiFi.setAutoReconnect(false);
      WiFi.begin(ssid, password[0] ? password : nullptr);
      phase = Phase::Joining;
      phaseAtMs = nowMs;
      result = TurnHub::HomeWifiResult::Checking;
      serialLog.println("ATLAS|HOME_WIFI|JOINING");
      return;
    case Phase::Joining:
      if (WiFi.status() == WL_CONNECTED) {
        networkChannel = static_cast<uint8_t>(WiFi.channel());
        serialLog.printf("ATLAS|HOME_WIFI|JOINED|channel=%u|table=%u\n",
            static_cast<unsigned>(networkChannel), static_cast<unsigned>(tableChannel));
        if (!startFetch()) { finish(nowMs, TurnHub::HomeWifiResult::FeedFailed); return; }
        phase = Phase::Fetching;
        phaseAtMs = nowMs;
        return;
      }
      // Give up when the network doesn't answer or a match starts.
      if (nowMs - phaseAtMs >= TurnHub::HOME_WIFI_JOIN_TIMEOUT_MS || !tableIdle()) {
        leave();
        result = TurnHub::HomeWifiResult::NoNetwork;
        schedule.finished(nowMs, false);
        serialLog.println("ATLAS|HOME_WIFI|DONE|noNetwork|LEFT");
      }
      return;
    case Phase::Fetching:
      if (!fetchDone) return;
      if (fetchOk) noteLatestFirmware(fetched);
      finish(nowMs, fetchOk ? TurnHub::HomeWifiResult::Checked : TurnHub::HomeWifiResult::FeedFailed);
      return;
    case Phase::Connected:
      // Dropped, or the router moved channel (which would pull the hotspot
      // and the Sigils with it): leave and go back to check-ins.
      if (!enabled || ssid[0] == '\0') { leave(); return; }
      if (WiFi.status() != WL_CONNECTED || WiFi.channel() != tableChannel) {
        serialLog.println("ATLAS|HOME_WIFI|CONNECTION_LOST");
        leave();
        return;
      }
      if (!schedule.due(nowMs, enabled, true, tableIdle())) return;
      if (!startFetch()) { finish(nowMs, TurnHub::HomeWifiResult::FeedFailed); return; }
      result = TurnHub::HomeWifiResult::Checking;
      phase = Phase::Fetching;
      phaseAtMs = nowMs;
      return;
  }
}

}  // namespace TurnHubAtlas
