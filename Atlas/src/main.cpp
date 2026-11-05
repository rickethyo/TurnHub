// Atlas entry point: defines the runtime objects, binds one handler per
// IntentType, brings up Wi-Fi/ESP-NOW/HTTP and runs the cooperative main
// loop. Games run on the hub phone (hosted_game.h). The handlers and adapters themselves live in
// the modules listed in atlas_app.h.

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "atlas_app.h"
#include "hosted_game.h"
#include "json_text.h"
#include "sigil_update_service.h"
#include "sigil_menu.h"
#include "touch_controls.h"
#include "atlas_board.h"
#include "atlas_battery.h"
#include "atlas_display.h"
#include "atlas_speaker.h"
#include "config.h"
#include "firmware_version.h"
#include "firmware_package.h"
#include "pairing_settings.h"
#include "runtime_diagnostics.h"
#include "http_diagnostics.h"
#include "profile_store.h"
#include "sd_card.h"
#include "secure_link_backend.h"
#include "serial_log.h"
#include "speaker_settings.h"
#include "table_code_setting.h"
#include "upload_approval.h"
#include "home_wifi.h"
#include "wifi_password_store.h"

#if defined(ARDUINO_ARCH_ESP32)
// Supported by the pinned core; app_main uses this value to create loopTask.
SET_LOOP_TASK_STACK_SIZE(AtlasConfig::LOOP_TASK_STACK_BYTES);
#endif

// This build's identity, read by tools/firmware/thfw.py when it packages
// firmware.bin for OTA (FIRMWARE_UPDATES.md).
TURNHUB_FIRMWARE_DESCRIPTOR(atlasFirmwareDescriptor, TurnHubFirmwarePackage::Product::Atlas,
    TurnHubFirmware::MAJOR, TurnHubFirmware::MINOR, TurnHubFirmware::PATCH,
    TurnHubProtocol::VERSION);

using TurnHub::serialLog;

WebServer server(AtlasConfig::HTTP_PORT);

namespace TurnHubAtlas {

// Construction order matters: the renderers and OTA hold references to
// sigilBus and server, which are defined first.
SigilBus sigilBus;
IntentDispatcher intents;
LedRenderer leds(sigilBus);
AudioController audio(sigilBus);
TurnHub::ToneOutput *hardwareTestSpeaker = nullptr;
OtaManager ota(server, otaAllowed);

bool espNowReady = false;
uint32_t pairingWindowMs = TurnHub::DEFAULT_PAIRING_WINDOW_MS;
TurnHub::PowerSettings powerSettings;
TurnHub::SetupStage setupStage = TurnHub::SetupStage::Complete;

namespace {
// The SD card generation the luxury store was last pointed for.
uint32_t luxuryStoreGeneration = 0;

// Heap and Wi-Fi client figures, logged periodically and whenever the phone
// count changes, so a crash under load leaves a trail in the serial log, the
// RAM log download and the SD diagnostics log (playtest 2026-09-29, item 1).
constexpr uint32_t HEAP_LOG_INTERVAL_MS = 60000;
uint32_t lastHeapLogMs = 0;
int lastStationCount = -1;

void logRuntimeHealth(uint32_t nowMs) {
  const int stations = WiFi.softAPgetStationNum();
  if (stations == lastStationCount && nowMs - lastHeapLogMs < HEAP_LOG_INTERVAL_MS) return;
  lastHeapLogMs = nowMs;
  lastStationCount = stations;
  serialLog.print("ATLAS|HEALTH|stations=");
  serialLog.print(stations);
  serialLog.print("|");
  serialLog.println(TurnHub::runtimeDiagnosticsJson());
  TurnHub::warnLowLoopStack(TurnHub::taskStackMinimumFreeBytes(), nowMs);
}

// Free heap after a start-up step, so each boot shows what every part costs
// (ATLAS|HEAP|<step>|free=<bytes>|largest=<bytes>).
void logHeapStep(const char *step) {
  uint32_t freeHeap = 0, largestBlock = 0;
#if defined(ARDUINO_ARCH_ESP32)
  freeHeap = esp_get_free_heap_size();
  largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
  serialLog.printf("ATLAS|HEAP|%s|free=%lu|largest=%lu\n", step,
      static_cast<unsigned long>(freeHeap), static_cast<unsigned long>(largestBlock));
}

// The owner-set password if one is stored, otherwise the shipped pre-setup
// default. The default is never written to NVS, so a stored password always
// means the owner chose it, and erasing NVS returns Atlas to the default.
String loadWifiPassword() {
  const String password = TurnHub::readStoredWifiPassword();
  if (TurnHub::validWifiPassword(password)) {
    serialLog.println("ATLAS|WIFI_AP|PASSWORD_STORE|LOADED");
    return password;
  }
  serialLog.println("ATLAS|WIFI_AP|PASSWORD_STORE|DEFAULT");
  return String(AtlasConfig::WIFI_DEFAULT_PASSWORD);
}

void handleRoot() {
  server.sendHeader("Location", "/portal");
  server.send(302, "text/plain", "TurnHub portal");
}

void serveDeveloperJson(const String &json) {
  server.sendHeader("Cache-Control", "no-store");
  if (TurnHubWebApi::requirePermission(server, TurnHubAccounts::Developer)) {
    server.send(200, "application/json", json);
  }
}

}  // namespace

// --- /api/v1/state -------------------------------------------------------------

namespace {
// Atlas's own state: the hub that holds the table and its game's outline.
// The revision moves when that outline changes; the battery and links are
// sampled (no revision change).
struct HubSummary {
  std::string name, gameId;
  turnhub::TableState state = turnhub::TableState::Lobby;
  size_t players = 0;
  bool held = false;
  bool operator!=(const HubSummary &o) const {
    return held != o.held || name != o.name || gameId != o.gameId || state != o.state || players != o.players;
  }
};
HubSummary lastHub;
uint32_t stateRevision = 1;

HubSummary hubSummary(uint32_t nowMs) {
  HubSummary hub;
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  if (!hosted.live(nowMs)) return hub;
  hub.held = true;
  hub.name = hosted.lease().name;
  hub.gameId = hosted.game().gameId;
  hub.state = hosted.game().state;
  hub.players = hosted.game().players.size();
  return hub;
}
}  // namespace

uint32_t clientRevision() {
  const HubSummary now = hubSummary(millis());
  if (now != lastHub) {
    lastHub = now;
    ++stateRevision;
  }
  return stateRevision;
}

String clientSnapshot(const String &atlasId, const char *bootId) {
  const uint32_t revision = clientRevision();
  String json;
  json.reserve(384);
  json = "{\"protocolVersion\":\"1\",\"atlasId\":\"";
  json += atlasId; json += "\",\"bootId\":\""; json += bootId;
  json += "\",\"revision\":"; json += String(revision);
  json += ",\"hub\":";
  if (!lastHub.held) {
    json += "null";
  } else {
    json += "{\"name\":\""; json += TurnHub::jsonEscape(lastHub.name.c_str());
    json += "\",\"gameId\":\""; json += TurnHub::jsonEscape(lastHub.gameId.c_str());
    json += "\",\"state\":\""; json += turnhub::stateName(lastHub.state);
    json += "\",\"players\":"; json += String(static_cast<unsigned>(lastHub.players));
    json += '}';
  }
  // Atlas's battery, sampled like the clocks (no revision change); null with
  // no cell. The app's header gauge draws it.
  const TurnHub::BatteryReading &battery = atlasBattery();
  json += ",\"battery\":";
  if (battery.present) {
    json += "{\"percent\":"; json += String(battery.percent);
    json += ",\"low\":"; json += battery.low ? "true" : "false";
    json += ",\"charging\":"; json += battery.charging ? "true" : "false";
    json += '}';
  } else {
    json += "null";
  }
  // Atlas's links, as its header icons show them (owner, 2026-11-03): the
  // hotspot, the home network and the Sigils online. Sampled like the battery.
  json += ",\"links\":{\"hotspot\":"; json += atlasHotspotOn() ? "true" : "false";
  json += ",\"home\":"; json += homeWifiConnected() ? "true" : "false";
  json += ",\"sigils\":"; json += String(static_cast<unsigned>(sigilBus.activeCount(millis())));
  json += "}}";
  return json;
}

// Dispatcher observer: runs after every Intent, accepted or not.
void observeIntent(const Intent &intent) {
  // Anything a player does keeps Atlas from sleeping on the cell.
  if (intent.actor.origin != IntentOrigin::System) noteAtlasActivity(millis());
}

// --- Intent bindings ---------------------------------------------------------------

// Binds exactly one handler per IntentType; the dispatcher rejects a second
// bind. Returns false (and logs BIND_FAILED) if any binding was refused.
bool configureIntentHandlers() {
  const struct {
    IntentType type;
    IntentDispatcher::Handler handler;
  } bindings[] = {
      {IntentType::PairRequest, handlePairRequestIntent},
      {IntentType::ForgetPairing, handleForgetPairingIntent},
      {IntentType::PairConfirm, handlePairConfirmIntent},
      {IntentType::ConfigurePairing, handleConfigurePairingIntent},
      {IntentType::ConfigureSpeaker, handleConfigureSpeakerIntent},
      {IntentType::ConfigurePower, handleConfigurePowerIntent},
      {IntentType::ConfigureUploadApproval, handleConfigureUploadApprovalIntent},
      {IntentType::ConfigureTableCode, handleConfigureTableCodeIntent},
      {IntentType::ResetTable, handleResetTableIntent},
      {IntentType::FactoryReset, handleFactoryResetIntent},
      {IntentType::UpdateSigil, handleUpdateSigilIntent},
      {IntentType::AdvanceSetup, handleAdvanceSetupIntent},
      {IntentType::Sleep, handleSleepIntent},
  };
  bool allBound = true;
  for (const auto &binding : bindings) {
    const bool bound = intents.bind(binding.type, binding.handler);
    serialLog.print("ATLAS|INTENT|");
    serialLog.print(TurnHub::intentName(binding.type));
    serialLog.println(bound ? "|BOUND" : "|BIND_FAILED");
    allBound = bound && allBound;
  }
  return allBound;
}

// --- HTTP ------------------------------------------------------------------------------

// Compact status for the diagnostics page. Clients use /api/v1/state.
void handleStatus() {
  TurnHub::HttpRequestTrace trace("/api/status");
  const uint32_t nowMs = millis();
  const HubSummary hub = hubSummary(nowMs);

  // HTTP is serialized on loopTask. No worker/callback uses this workspace,
  // and WebServer::send consumes it before the next request can enter.
  static char json[1024];
  const int length = snprintf(
      json,
      sizeof(json),
      "{\"presenceActive\":%s,\"presenceCodeShown\":%s,\"sigils\":%u,\"hub\":%s,"
      "\"state\":\"%s\",\"players\":%u,\"espNow\":%s,\"firmware\":\"%s\","
      "\"build\":\"%s %s\",\"otaStateAllowed\":%s}",
      anyPresenceActive(nowMs) ? "true" : "false",
      pendingPresenceCode(nowMs) != nullptr ? "true" : "false",
      static_cast<unsigned>(sigilBus.activeCount(nowMs)),
      hub.held ? "true" : "false",
      turnhub::stateName(hub.state),
      static_cast<unsigned>(hub.players),
      espNowReady ? "true" : "false",
      TurnHubFirmware::VERSION,
      TurnHubFirmware::BUILD_DATE,
      TurnHubFirmware::BUILD_TIME,
      betweenGames(nowMs) ? "true" : "false");

  server.sendHeader("Cache-Control", "no-store");
  if (length < 0 || static_cast<size_t>(length) >= sizeof(json)) {
    server.send(503, "application/json", "{\"error\":\"Status response exceeded capacity\"}");
    return;
  }
  server.send(200, "application/json", json);
}

// Only a new or factory-reset Atlas advertises the provisioning network.
// Home-network failures never turn it back on after setup.
bool startHotspot(const String &wifiPassword) {
  constexpr bool HIDDEN_SSID = false;
  constexpr int MAX_STATIONS = 8;
  // The saved table channel (6 unless the owner picks another). Once Atlas
  // joins the home network the radio follows the router's channel instead
  // (home_wifi.h); Sigils search for it (channel_search.h).
  const uint8_t channel = TurnHub::readTableChannel();
  if (!WiFi.softAP(AtlasConfig::WIFI_SSID, wifiPassword.c_str(), channel,
          HIDDEN_SSID, MAX_STATIONS)) {
    serialLog.println("ATLAS|WIFI_AP|ERROR");
    return false;
  }
  serialLog.print("ATLAS|WIFI_AP|READY|");
  serialLog.print(AtlasConfig::WIFI_SSID);
  serialLog.print("|");
  serialLog.print(WiFi.softAPIP());
  serialLog.printf("|channel=%u\n", static_cast<unsigned>(channel));
  serialLog.println("ATLAS|WIFI_AP|SECURITY|WPA2-PSK");
  // The port keeps showing the password for the owner at the table; the
  // downloadable copy never contains it.
  serialLog.printlnRedacted(
      (String("ATLAS|WIFI_AP|PASSWORD|") + wifiPassword).c_str(),
      "ATLAS|WIFI_AP|PASSWORD|<redacted>");
  return true;
}

void startNetworking() {
  const String wifiPassword = loadWifiPassword();
  if (wifiPassword.length() < AtlasConfig::WIFI_PASSWORD_MIN_LENGTH) {
    serialLog.println("ATLAS|WIFI_AP|PASSWORD|ERROR");
    return;
  }
  const bool homeOnly = setupStage != TurnHub::SetupStage::Welcome;
  WiFi.mode(homeOnly ? WIFI_STA : WIFI_AP_STA);
  if (homeOnly) serialLog.println("ATLAS|WIFI_AP|OFF|SETUP_COMPLETE");
  else if (!startHotspot(wifiPassword)) return;
  // Wi-Fi modem power saving can make the AP answer phones late or drop them
  // (the app timing out back to back, 2026-10-13). Costs some battery.
  WiFi.setSleep(false);

  serialLog.print("ATLAS|MAC|");
  serialLog.println(WiFi.macAddress());
  logHeapStep("WIFI_AP");

  espNowReady = sigilBus.begin();
  logHeapStep("ESP_NOW");

  registerWebCallbacks();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/diagnostics", HTTP_GET, []() {
    // Runtime figures plus the optional microSD card's state.
    String json = TurnHub::runtimeDiagnosticsJson();
    json = json.substring(0, json.length() - 1) + ",\"sdCard\":" +
           sdCardDiagnosticsJson() + "}";
    serveDeveloperJson(json);
  });
  server.on("/api/diagnostics/activity", HTTP_GET, []() {
    serveDeveloperJson(TurnHub::activityJson());
  });
  ota.begin();
  beginSigilUpdates(AtlasConfig::WIFI_SSID, wifiPassword.c_str());
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  logHeapStep("WEB");

  serialLog.println("ATLAS|WEB|READY");
}

}  // namespace TurnHubAtlas

using namespace TurnHubAtlas;

void setup() {
  // Asleep on the cell, Atlas wakes each minute to look for USB: without it,
  // back to sleep before anything starts.
  stayAsleepWithoutUsb();
  // Back from Sleep: the wake pins return to the digital GPIO driver and the
  // backlight's hold is released before anything configures them.
  releaseSleepWakePins();
  Serial.begin(115200);
  // Framework errors (log_e, ESP-IDF) also reach the RAM and SD logs.
  TurnHub::installFrameworkLogCapture();
  delay(250);
  beginFrontPanel();
  beginAtlasDisplay();

  serialLog.println();
  serialLog.print("ATLAS|BOOT|");
  serialLog.println(TurnHubFirmware::VERSION);
  // A real read of the descriptor, so the linker keeps it in the image.
  serialLog.print("ATLAS|FW_PRODUCT|");
  serialLog.println(*reinterpret_cast<const volatile uint8_t *>(&atlasFirmwareDescriptor.product));
  logAtlasBoard();
  serialLog.print("ATLAS|RESET_REASON|");
  serialLog.println(TurnHub::resetReason());
  serialLog.print("ATLAS|DIAGNOSTICS|");
  serialLog.println(TurnHub::runtimeDiagnosticsJson());
  TurnHub::recordActivity("boot", TurnHub::resetReason());
  logHeapStep("DISPLAY");
  // Secure-link crypto check against published vectors (PAIRING_AND_SECURE_LINK.md), which
  // the radio depends on; logged so each board's result is on record.
  runSecureLinkSelfTest();
  logHeapStep("SECURE_LINK");
  // Optional storage: a missing or failed card is logged and never blocks play.
  beginSdCard();
  logHeapStep("SD_CARD");
  // Luxury records (detailed statistics) go to the card; without one Atlas
  // keeps only the core counts.
  // A card inserted or pulled later is picked up by refreshSdLuxuryStore().
  luxuryStoreGeneration = sdCardGeneration();
  TurnHubProfiles::setLuxuryStore(sdBlobStore());
  TurnHubProfiles::begin();

  configureIntentHandlers();
  sigilBus.setSeatedQuery([](uint8_t id) { return sigilInGame(id, millis()); });
  intents.setObserver(observeIntent);
  logHeapStep("PROFILES");

  // A missing or unreadable setting keeps the 60-second default (and minimum).
  const auto pairingStatus = TurnHub::loadPairingWindow(pairingWindowMs);
  if (pairingStatus != TurnHubStorage::Status::Ok &&
      pairingStatus != TurnHubStorage::Status::NotFound) {
    serialLog.println("ATLAS|PAIRING|WINDOW|STORAGE_ERROR");
  }
  beginFirstRunSetup();
  // The speaker plays table-wide cues; a missing or unreadable volume keeps
  // the default.
  uint8_t speakerVolume = TurnHub::DEFAULT_SPEAKER_VOLUME;
  const auto speakerStatus = TurnHub::loadSpeakerVolume(speakerVolume);
  if (speakerStatus != TurnHubStorage::Status::Ok &&
      speakerStatus != TurnHubStorage::Status::NotFound) {
    speakerVolume = TurnHub::DEFAULT_SPEAKER_VOLUME;
    serialLog.println("ATLAS|SPEAKER|VOLUME|STORAGE_ERROR");
  }
  // Screen off and auto sleep; a missing or unreadable record keeps the
  // defaults (power_settings.h).
  TurnHub::PowerSettings power;
  const auto powerStatus = TurnHub::loadPowerSettings(power);
  if (powerStatus == TurnHubStorage::Status::Ok) {
    powerSettings = power;
  } else if (powerStatus != TurnHubStorage::Status::NotFound) {
    serialLog.println("ATLAS|POWER|STORAGE_ERROR");
  }
  // The table code is an optional extra step; a missing or unreadable
  // setting keeps it off.
  bool codeRequired = TurnHub::DEFAULT_TABLE_CODE_REQUIRED;
  const auto codeStatus = TurnHub::loadTableCodeRequired(codeRequired);
  if (codeStatus == TurnHubStorage::Status::Ok) {
    tableCodeRequired = codeRequired;
  } else if (codeStatus != TurnHubStorage::Status::NotFound) {
    serialLog.println("ATLAS|PRESENCE|SETTING|STORAGE_ERROR");
  }
  uploadApprovalRequired = TurnHub::DEFAULT_UPLOAD_APPROVAL_REQUIRED;
  bool approvalRequired = uploadApprovalRequired;
  const auto approvalStatus = TurnHub::loadUploadApprovalRequired(approvalRequired);
  if (approvalStatus == TurnHubStorage::Status::Ok) uploadApprovalRequired = approvalRequired;
  else if (approvalStatus != TurnHubStorage::Status::NotFound)
    serialLog.println("ATLAS|UPLOAD_APPROVAL|SETTING|STORAGE_ERROR");
  hardwareTestSpeaker = beginAtlasSpeaker();
  audio.setSpeaker(hardwareTestSpeaker);
  TurnHubWebApi::configureHardwareTests([](const String &test) {
    if (test == "screen") return startAtlasScreenTest();
    if (test == "buzzer") {
      if (!hardwareTestSpeaker) return false;
      static uint32_t lastTestMs = 0;
      const uint32_t nowMs = millis();
      if (lastTestMs && nowMs - lastTestMs < 1000) return false;
      lastTestMs = nowMs;
      hardwareTestSpeaker->tone(880, 700, 1);
      noteAtlasActivity(nowMs);
      return true;
    }
    return false;
  });
  audio.setSpeakerVolume(speakerVolume);
  logHeapStep("SPEAKER");
  beginAtlasBattery();
  startNetworking();
  serialLog.println("ATLAS|READY");
}

// The SD worker mounted or dropped a card (hot-plug): point detailed
// statistics at the card, or at nothing. Application task only, like every
// luxury-store call.
void refreshSdLuxuryStore() {
  const uint32_t generation = sdCardGeneration();
  if (generation == luxuryStoreGeneration) return;
  luxuryStoreGeneration = generation;
  TurnHubStorage::BlobStore *store = sdBlobStore();
  TurnHubProfiles::setLuxuryStore(store);
  serialLog.println(store ? "ATLAS|SD|STATS|CARD" : "ATLAS|SD|STATS|CORE_ONLY");
  // The planar catalog the app saved here, for digital Planechase decks.
  if (store && TurnHubCards::loadPlanes()) serialLog.printf("ATLAS|PLANES|LOADED|%u\n", TurnHubCards::planeCount());
}

// The SD worker saved (or couldn't save) a plane card: the screen's toast.
void showPlaneCardProgress(uint32_t nowMs) {
  char line[48];
  if (TurnHubCards::takePlaneProgress(line, sizeof(line))) TurnHubAtlas::showSyncStatus(nowMs, line);
}

// Bench diagnostics (off in normal builds): -DTURNHUB_LOOP_TIMING=1 logs any
// loop() step that holds the loop for LOOP_STEP_WARN_MS or more, to find what
// freezes the screen (ATLAS|LOOP|SLOW|<step>|<ms>).
#ifndef TURNHUB_LOOP_TIMING
#define TURNHUB_LOOP_TIMING 0
#endif
#if TURNHUB_LOOP_TIMING
constexpr uint32_t LOOP_STEP_WARN_MS = 300;
#define LOOP_STEP(call) do { const uint32_t stepStart_ = millis(); call; \
    const uint32_t stepMs_ = millis() - stepStart_; \
    if (stepMs_ >= LOOP_STEP_WARN_MS) serialLog.printf("ATLAS|LOOP|SLOW|%s|%lu\n", #call, static_cast<unsigned long>(stepMs_)); } while (0)
#define LOOP_STEP_BEGIN() const uint32_t tablesStart_ = millis()
#define LOOP_STEP_END(name) do { const uint32_t stepMs_ = millis() - tablesStart_; \
    if (stepMs_ >= LOOP_STEP_WARN_MS) serialLog.printf("ATLAS|LOOP|SLOW|%s|%lu\n", name, static_cast<unsigned long>(stepMs_)); } while (0)
#else
#define LOOP_STEP(call) call
#define LOOP_STEP_BEGIN() do {} while (0)
#define LOOP_STEP_END(name) do {} while (0)
#endif

void loop() {
#if TURNHUB_LOOP_TIMING
  // Time outside loop() (between passes: Arduino's own work, other tasks).
  static uint32_t lastPassEndMs = 0;
  if (lastPassEndMs && millis() - lastPassEndMs >= LOOP_STEP_WARN_MS)
    serialLog.printf("ATLAS|LOOP|SLOW|between|%lu\n", static_cast<unsigned long>(millis() - lastPassEndMs));
#endif
  LOOP_STEP(processSigilEvents());
  LOOP_STEP(serviceSigilUpdates(millis()));
  LOOP_STEP(serviceHomeWifi(millis()));
  LOOP_STEP(server.handleClient());
  LOOP_STEP(refreshSdLuxuryStore());

  const uint32_t nowMs = millis();
  LOOP_STEP(readSerialCommands(nowMs));
  LOOP_STEP(updatePairingWindow(nowMs));
  LOOP_STEP(updateBootButton(digitalRead(AtlasConfig::BOOT_BUTTON_PIN) == LOW, nowMs));
  LOOP_STEP(showPlaneCardProgress(nowMs));
  LOOP_STEP(serviceAtlasDisplay(nowMs));
  LOOP_STEP(serviceFactoryReset(nowMs));
  LOOP_STEP(serviceAutoSleep(nowMs));
  LOOP_STEP(serviceSleep(nowMs));
  LOOP_STEP_BEGIN();
  // The Sigils follow the hub's game; with no hub they show nobody seated.
  // A change either way redraws them from scratch.
  static bool sigilsHosted = false;
  static const turnhub::Game noGame;
  const bool hostedNow = TurnHubHosted::hostedTable().live(nowMs);
  if (hostedNow != sigilsHosted) {
    sigilsHosted = hostedNow;
    leds.invalidateAll();
    for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) invalidateSigilMenu(id);
  }
  leds.renderHosted(hostedNow ? TurnHubHosted::hostedTable().game() : noGame, nowMs);
  LOOP_STEP_END("sigils");
  LOOP_STEP(audio.update(nowMs));
  LOOP_STEP(serviceAtlasSpeaker(nowMs));
  LOOP_STEP(serviceAtlasBattery(nowMs));
  LOOP_STEP(syncSigilMenus(nowMs));
  LOOP_STEP(ota.update(nowMs));
  LOOP_STEP(logRuntimeHealth(nowMs));

  delay(1);
#if TURNHUB_LOOP_TIMING
  lastPassEndMs = millis();
#endif
}
