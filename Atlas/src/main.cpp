// Atlas entry point: defines the runtime objects, binds one handler per
// IntentType, brings up Wi-Fi/ESP-NOW/HTTP, restores an interrupted match and
// runs the cooperative main loop. The handlers and adapters themselves live in
// the modules listed in atlas_app.h.

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "atlas_app.h"
#include "sigil_update_service.h"
#include "sigil_menu.h"
#include "profile_picker.h"
#include "commander_picker.h"
#include "atlas_display.h"
#include "atlas_speaker.h"
#include "config.h"
#include "firmware_version.h"
#include "firmware_package.h"
#include "game_recovery.h"
#include "game_settings_store.h"
#include "pairing_settings.h"
#include "runtime_diagnostics.h"
#include "http_diagnostics.h"
#include "profile_store.h"
#include "sd_card.h"
#include "secure_link_backend.h"
#include "serial_log.h"
#include "speaker_settings.h"
#include "wifi_password_store.h"

#if defined(ARDUINO_ARCH_ESP32)
// Supported by the pinned core; app_main uses this value to create loopTask.
SET_LOOP_TASK_STACK_SIZE(AtlasConfig::LOOP_TASK_STACK_BYTES);
#endif

// This build's identity, read by tools/firmware/thfw.py when it packages
// firmware.bin for OTA (SIGIL_OTA.md).
TURNHUB_FIRMWARE_DESCRIPTOR(atlasFirmwareDescriptor, TurnHubFirmwarePackage::Product::Atlas,
    TurnHubFirmware::MAJOR, TurnHubFirmware::MINOR, TurnHubFirmware::PATCH,
    TurnHubProtocol::VERSION);

using TurnHub::serialLog;

WebServer server(AtlasConfig::HTTP_PORT);

namespace TurnHubAtlas {

// Construction order matters: the renderers and OTA hold references to
// sigilBus and server, which are defined first.
SigilBus sigilBus(AtlasConfig::WIFI_CHANNEL);
IntentDispatcher intents;
LedRenderer leds(sigilBus);
AudioController audio(sigilBus);
OtaManager ota(server, otaAllowed);

GameTable tables[MAX_GAME_TABLES];
Lobby &lobby = tables[0].lobby;
GameEngine &game = tables[0].game;
TurnHub::ClientState &clientState = tables[0].clientState;
HubState &hubState = tables[0].hubState;
PendingPassState &pendingPass = tables[0].pendingPass;
uint32_t &countdownStartedAtMs = tables[0].countdownStartedAtMs;
int8_t &lastCountdownSecond = tables[0].lastCountdownSecond;
uint8_t &eliminationTargetPlayer = tables[0].eliminationTargetPlayer;
TurnTimerCueState &turnTimerCue = tables[0].turnTimerCue;

TurnHub::GameSettings nextGameSettings;
bool gameSettingsAvailable = true;
bool espNowReady = false;
uint32_t pairingWindowMs = TurnHub::DEFAULT_PAIRING_WINDOW_MS;
TurnHub::SetupStage setupStage = TurnHub::SetupStage::Complete;

namespace {
// The SD card generation the luxury store was last pointed for.
uint32_t luxuryStoreGeneration = 0;

// Polling cadence for the elapsed-clock recovery checkpoint (see below).
constexpr uint32_t RECOVERY_POLL_INTERVAL_MS = 1000;
uint32_t lastRecoveryPollMs = 0;

// Catches the "periodically checkpoint elapsed clocks" requirement for a long
// turn with no dispatched intents in between: GameRecovery::save() only
// writes when >=60s have passed since the last save of a running match, so
// polling this once a second is cheap (no encode-vs-previous work happens
// between checkpoints) and never spams NVS.
void updateGameRecoveryClock(uint32_t nowMs) {
  if (nowMs - lastRecoveryPollMs < RECOVERY_POLL_INTERVAL_MS) return;
  lastRecoveryPollMs = nowMs;
  TurnHub::checkpointGame(game, nowMs);
}

// Heap and Wi-Fi client figures, logged periodically and whenever the phone
// count changes, so a crash under load leaves a trail in the serial log, the
// RAM log download and the SD diagnostics log (playtest 2026-09-29, item 1).
constexpr uint32_t HEAP_LOG_INTERVAL_MS = 60000;
uint32_t lastHeapLogMs = 0;
int lastStationCount = -1;

// Names in /api/v1/state are re-read when a seat's occupant changes; this
// also picks up a rename (profile page, Sigil picker) within a few seconds.
constexpr uint32_t CLIENT_NAME_REFRESH_MS = 5000;
uint32_t lastClientNameRefreshMs = 0;

void refreshClientNames(uint32_t nowMs) {
  if (nowMs - lastClientNameRefreshMs < CLIENT_NAME_REFRESH_MS) return;
  lastClientNameRefreshMs = nowMs;
  clientState.refreshNames();
}

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

// Restores an interrupted match, if any, before the portal/radio boundary
// opens. A reboot always finds Atlas in an empty Lobby unless a valid,
// unfinished checkpoint says otherwise; any storage problem fails safe to
// that same empty Lobby rather than risking ambiguous game state. Existing
// profiles/statistics are untouched either way -- this only concerns the
// small active-match cache.
void restoreInterruptedMatch() {
  using TurnHubStorage::Status;
  const auto recoveryStatus = TurnHub::beginGameRecovery(game, lobby, millis());
  if (recoveryStatus == Status::Ok && game.hasPlayers()) {
    // A restored match is always paused (never running) and never charges
    // downtime -- GameEngine::restoreCheckpoint() already rebased the
    // elapsed clocks into this boot's millis() domain. The existing
    // Pause/Resume controls (physical and browser) are the "Resume" side of
    // recovery. There is deliberately no "Discard" affordance yet -- see the
    // STAGED_CHANGES note on interrupted-match recovery.
    hubState = game.gameOver() ? HubState::GameOver : HubState::Paused;
    leds.invalidateAll();
    serialLog.print("ATLAS|RECOVERY|OUTCOME|RESTORED|STATE|");
    serialLog.println(stateName(hubState));
  } else if (recoveryStatus == Status::NotFound) {
    serialLog.println("ATLAS|RECOVERY|OUTCOME|NO_SAVED_MATCH");
  } else if (recoveryStatus == Status::Ok) {
    // A valid record was found and decoded, but it held no active match
    // (e.g. checkpointed while Atlas was sitting in an empty Lobby).
    serialLog.println("ATLAS|RECOVERY|OUTCOME|EMPTY_RECORD");
  } else {
    serialLog.print("ATLAS|RECOVERY|OUTCOME|FAILSAFE|");
    serialLog.println(TurnHub::storageStatusName(recoveryStatus));
  }
  observeClientState();
}

}  // namespace

// --- Client state projection ----------------------------------------------------

void observeClientState() {
  TurnHub::ClientPending pending;
  pending.passPlayer = pendingPass.active ? pendingPass.seat.playerNumber : 0;
  pending.passStartedMs = pendingPass.active ? pendingPass.requestedAtMs : 0;
  pending.countdownStartedMs = hubState == HubState::Starting ? countdownStartedAtMs : 0;
  pending.eliminationTarget = eliminationTargetPlayer;
  clientState.observe(hubState, lobby, game, nextGameSettings, pending);
}

uint32_t clientRevision() {
  observeClientState();
  return clientState.revision();
}

String clientSnapshot(const String &atlasId, const char *bootId) {
  observeClientState();
  return clientState.json(atlasId, bootId, game, millis(), PASS_GRACE_MS);
}

// Dispatcher observer: runs after every Intent, accepted or not.
void observeIntent(const Intent &intent) {
  // The loop dispatches expiry every frame. Its no-op path must not rebuild
  // the complete Commander matrix or re-encode the recovery checkpoint (the
  // once-a-second updateGameRecoveryClock poll still runs). The client
  // projection still holds the old requests here, so expirationDue() says
  // whether this tick just settled one. Other completed handlers are
  // infrequent.
  if (intent.type == IntentType::ExpireLifeChanges && !clientState.expirationDue(millis())) return;
  observeClientState();
  // Persist a checkpoint after every dispatched intent. GameRecovery::save()
  // only actually touches NVS when the encoded game state changed or the
  // periodic clock checkpoint is due, so this is cheap to call unconditionally
  // -- including after a rejected intent, where nothing changed and it is a
  // no-op. This is the "after accepted semantic transitions" hook the
  // interrupted-match recovery design calls for.
  TurnHub::checkpointGame(game, millis());
}

// --- Intent bindings ---------------------------------------------------------------

// Binds exactly one handler per IntentType; the dispatcher rejects a second
// bind. Returns false (and logs BIND_FAILED) if any binding was refused.
bool configureIntentHandlers() {
  const struct {
    IntentType type;
    IntentDispatcher::Handler handler;
  } bindings[] = {
      {IntentType::Moderate, handleModerateIntent},
      {IntentType::ConfigureGame, handleGameSettingsIntent},
      {IntentType::ChangeLife, handleChangeLifeIntent},
      {IntentType::Pass, handlePassIntent},
      {IntentType::Pause, handlePauseIntent},
      {IntentType::Resume, handleResumeIntent},
      {IntentType::Concede, handleConcedeIntent},
      {IntentType::TogglePause, handleTogglePauseIntent},
      {IntentType::ClaimWin, handleClaimWinIntent},
      {IntentType::ConfirmWin, handleConfirmWinIntent},
      {IntentType::DenyWin, handleDenyWinIntent},
      {IntentType::SelectStarter, handleSelectStarterIntent},
      {IntentType::Join, handleSeatMembershipIntent},
      {IntentType::Leave, handleSeatMembershipIntent},
      {IntentType::JoinProfile, handleProfileParticipationIntent},
      {IntentType::LeaveProfile, handleProfileParticipationIntent},
      {IntentType::BindProfile, handleProfileParticipationIntent},
      {IntentType::PickProfile, handleProfileParticipationIntent},
      {IntentType::ArmStart, handleStartIntent},
      {IntentType::StartGame, handleStartIntent},
      {IntentType::CancelStart, handleCancelStartIntent},
      {IntentType::CompleteStart, handleCompleteStartIntent},
      {IntentType::Rematch, handleResetIntent},
      {IntentType::ResetGame, handleResetIntent},
      {IntentType::BeginElimination, handleEliminationIntent},
      {IntentType::CycleElimination, handleEliminationIntent},
      {IntentType::CancelElimination, handleEliminationIntent},
      {IntentType::Eliminate, handleEliminationIntent},
      {IntentType::CancelPass, handleCancelPassIntent},
      {IntentType::CommitPass, handleCommitPassIntent},
      {IntentType::PairRequest, handlePairRequestIntent},
      {IntentType::RequestLifeChange, handleCounterIntent},
      {IntentType::RespondLifeChange, handleCounterIntent},
      {IntentType::ExpireLifeChanges, handleExpireLifeChangesIntent},
      {IntentType::ChangeCounter, handleCounterIntent},
      {IntentType::RecordCommanderHit, handleCounterIntent},
      {IntentType::UndoCommanderHit, handleCounterIntent},
      {IntentType::EndMatch, handleEndMatchIntent},
      {IntentType::MasterPass, handleMasterPassIntent},
      {IntentType::ForgetPairing, handleForgetPairingIntent},
      {IntentType::PairConfirm, handlePairConfirmIntent},
      {IntentType::ConfigurePairing, handleConfigurePairingIntent},
      {IntentType::ConfigureSpeaker, handleConfigureSpeakerIntent},
      {IntentType::ResetTable, handleResetTableIntent},
      {IntentType::FactoryReset, handleFactoryResetIntent},
      {IntentType::UpdateSigil, handleUpdateSigilIntent},
      {IntentType::MoveSeat, handleMoveSeatIntent},
      {IntentType::SetSeatSide, handleSetSeatSideIntent},
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
  PlayerSeat selected;
  const uint8_t starter = lobby.selectedStarter(selected)
      ? selected.playerNumber
      : (game.hasPlayers() ? game.starterPlayerNumber() : 0);
  const uint8_t active = (hubState == HubState::Running || hubState == HubState::Paused)
      ? game.activePlayerNumber()
      : 0;
  const uint8_t players = game.hasPlayers() ? game.playerCount() : lobby.playerCount();
  const bool otaStateAllowed = hubState == HubState::Lobby || hubState == HubState::GameOver;
  const uint32_t nowMs = millis();
  const uint32_t passElapsed = pendingPass.active ? nowMs - pendingPass.requestedAtMs : 0;
  const uint32_t passGraceRemainingMs = pendingPass.active && passElapsed < PASS_GRACE_MS
      ? PASS_GRACE_MS - passElapsed
      : 0;

  // HTTP is serialized on loopTask. No worker/callback uses this workspace,
  // and WebServer::send consumes it before the next request can enter.
  static char json[1024];
  const int length = snprintf(
      json,
      sizeof(json),
      "{\"presenceActive\":%s,\"presenceCodeShown\":%s,\"sigils\":%u,\"players\":%u,"
      "\"state\":\"%s\",\"starter\":%u,"
      "\"active\":%u,\"winner\":%u,\"eliminationTarget\":%u,"
      "\"winConfirm\":%u,\"passPending\":%u,\"passGraceMs\":%lu,"
      "\"turnTimerMs\":%lu,\"turnElapsedMs\":%lu,\"turnRemainingMs\":%lu,\"timerPhase\":\"%s\","
      "\"espNow\":%s,\"firmware\":\"%s\","
      "\"build\":\"%s %s\",\"otaStateAllowed\":%s}",
      anyPresenceActive(nowMs) ? "true" : "false",
      pendingPresenceCode(nowMs) != nullptr ? "true" : "false",
      static_cast<unsigned>(sigilBus.activeCount(nowMs)),
      static_cast<unsigned>(players),
      stateName(hubState),
      static_cast<unsigned>(starter),
      static_cast<unsigned>(active),
      static_cast<unsigned>(game.winnerPlayerNumber()),
      static_cast<unsigned>(eliminationTargetPlayer),
      static_cast<unsigned>(game.nextWinConfirmationPlayerNumber()),
      static_cast<unsigned>(pendingPass.active ? pendingPass.seat.playerNumber : 0),
      static_cast<unsigned long>(passGraceRemainingMs),
      static_cast<unsigned long>(game.hasPlayers() ? game.turnTimerMs() : nextGameSettings.turnTimerMs),
      static_cast<unsigned long>(game.currentTurnElapsedMs(nowMs)),
      static_cast<unsigned long>(game.turnRemainingMs(nowMs)),
      TurnHub::turnTimerPhaseName(game.turnTimerPhase(nowMs)),
      espNowReady ? "true" : "false",
      TurnHubFirmware::VERSION,
      TurnHubFirmware::BUILD_DATE,
      TurnHubFirmware::BUILD_TIME,
      otaStateAllowed ? "true" : "false");

  server.sendHeader("Cache-Control", "no-store");
  if (length < 0 || static_cast<size_t>(length) >= sizeof(json)) {
    server.send(503, "application/json", "{\"error\":\"Status response exceeded capacity\"}");
    return;
  }
  server.send(200, "application/json", json);
}

void startNetworking() {
  WiFi.mode(WIFI_AP_STA);

  const String wifiPassword = loadWifiPassword();
  if (wifiPassword.length() < AtlasConfig::WIFI_PASSWORD_MIN_LENGTH) {
    serialLog.println("ATLAS|WIFI_AP|PASSWORD|ERROR");
    return;
  }

  constexpr bool HIDDEN_SSID = false;
  constexpr int MAX_STATIONS = 8;
  if (!WiFi.softAP(AtlasConfig::WIFI_SSID, wifiPassword.c_str(), AtlasConfig::WIFI_CHANNEL,
          HIDDEN_SSID, MAX_STATIONS)) {
    serialLog.println("ATLAS|WIFI_AP|ERROR");
    return;
  }

  serialLog.print("ATLAS|WIFI_AP|READY|");
  serialLog.print(AtlasConfig::WIFI_SSID);
  serialLog.print("|");
  serialLog.println(WiFi.softAPIP());
  serialLog.println("ATLAS|WIFI_AP|SECURITY|WPA2-PSK");
  // The port keeps showing the password for the owner at the table; the
  // downloadable copy never contains it.
  serialLog.printlnRedacted(
      (String("ATLAS|WIFI_AP|PASSWORD|") + wifiPassword).c_str(),
      "ATLAS|WIFI_AP|PASSWORD|<redacted>");

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
  // Back from Sleep: the wake pins return to the digital GPIO driver and the
  // backlight's hold is released before anything configures them.
  releaseSleepWakePins();
  Serial.begin(115200);
  delay(250);
  beginFrontPanel();
  beginAtlasDisplay();

  serialLog.println();
  serialLog.print("ATLAS|BOOT|");
  serialLog.println(TurnHubFirmware::VERSION);
  // A real read of the descriptor, so the linker keeps it in the image.
  serialLog.print("ATLAS|FW_PRODUCT|");
  serialLog.println(*reinterpret_cast<const volatile uint8_t *>(&atlasFirmwareDescriptor.product));
  serialLog.print("ATLAS|RESET_REASON|");
  serialLog.println(TurnHub::resetReason());
  serialLog.print("ATLAS|DIAGNOSTICS|");
  serialLog.println(TurnHub::runtimeDiagnosticsJson());
  TurnHub::recordActivity("boot", TurnHub::resetReason());
  logHeapStep("DISPLAY");
  // Secure-link crypto check against published vectors (SECURE_LINK.md), which
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
  sigilBus.setSeatedQuery([](uint8_t id) { return lobby.isJoined(id) || game.controllerInGame(id); });
  clientState.setNameLookup(displayNameForTableSeat);
  observeClientState();
  intents.setObserver(observeIntent);
  restoreInterruptedMatch();
  logHeapStep("PROFILES_AND_RECOVERY");

  const auto settingsStatus = TurnHub::loadGameSettings(nextGameSettings);
  gameSettingsAvailable = settingsStatus == TurnHubStorage::Status::Ok ||
      settingsStatus == TurnHubStorage::Status::NotFound;
  if (!gameSettingsAvailable) serialLog.println("ATLAS|GAME_SETTINGS|STORAGE_ERROR");
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
  audio.setSpeaker(beginAtlasSpeaker());
  audio.setSpeakerVolume(speakerVolume);
  logHeapStep("SPEAKER");
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
}

void loop() {
  processSigilEvents();
  serviceSigilUpdates(millis());
  server.handleClient();
  refreshSdLuxuryStore();

  const uint32_t nowMs = millis();
  updatePairingWindow(nowMs);
  updateBootButton(digitalRead(AtlasConfig::BOOT_BUTTON_PIN) == LOW, nowMs);
  serviceAtlasDisplay(nowMs);
  serviceFactoryReset(nowMs);
  serviceSleep(nowMs);
  updatePendingPass(nowMs);
  updateCountdown(nowMs);
  updateTurnTimerCues(nowMs);
  dispatchSystemIntent(IntentType::ExpireLifeChanges);
  updateGameRecoveryClock(nowMs);
  updateSigilAccessibility(nowMs);
  audio.update(nowMs);
  serviceAtlasSpeaker(nowMs);
  leds.render(hubState, lobby, game, countdownStartedAtMs, eliminationTargetPlayer,
      game.nextWinConfirmationPlayerNumber(), nowMs);
  syncSigilMenus(nowMs);
  syncProfilePickers(nowMs);
  syncCommanderPickers(nowMs);
  ota.update(nowMs);
  refreshClientNames(nowMs);
  logRuntimeHealth(nowMs);

  delay(1);
}
