#pragma once

// Atlas application layer: the runtime objects and helpers shared by the
// Intent handlers and the transport adapters.
//
// Phone-hosted games (owner, 2026-11-04): the hub phone or tablet runs every
// game with the shared rules core; Atlas shows it on its screen and Sigils,
// queues their presses for the hub and keeps records (hosted_game.h). Atlas
// itself decides only device matters: pairing, settings, setup, sleep and
// resets.
//
// Module layout (everything below lives in namespace TurnHubAtlas):
//
//   main.cpp               runtime object definitions, handler bindings,
//                          networking/HTTP bring-up, setup() and loop()
//   app_context.cpp        Intent origin names, the System Intent builder and
//                          the between-games checks
//   device_intents.cpp     pairing (open, confirm, forget, window), speaker,
//                          power, table code, upload approval, first-run
//                          setup, Return to lobby, factory reset and sleep
//   hosted_sigils.cpp      the Sigils' menus and keys in a hosted game
//   sigil_input.cpp        ESP-NOW event adapter
//   web_adapters.cpp       browser callbacks registered with TurnHubWebApi
//   front_panel.cpp        pairing window, table presence codes, BOOT button
//   touch_controls.cpp     touchscreen buttons (adapter) and the TFT's
//                          screen model (touch_controls.h)
//
// Ownership rule (ARCHITECTURAL_INVARIANTS.md): handle*Intent functions are
// the only code that changes Atlas's device state. Adapters build an Intent
// and dispatch it; tests/host/audit_adapters.py fails the build if one
// bypasses the dispatcher.

#include <Arduino.h>
#include <WebServer.h>

#include "audio_controller.h"
#include "intent.h"
#include "intent_dispatcher.h"
#include "led_renderer.h"
#include "ota_manager.h"
#include "power_settings.h"
#include "protocol.h"
#include "setup_stage.h"
#include "sigil_bus.h"
#include "turnhub_types.h"
#include "update_notice.h"
#include "web_api.h"

// The portal/API server is shared with OtaManager and the host test harness.
extern WebServer server;

namespace TurnHubAtlas {

using TurnHub::AudioController;
using TurnHub::Intent;
using TurnHub::IntentDispatcher;
using TurnHub::IntentOrigin;
using TurnHub::IntentResult;
using TurnHub::IntentStatus;
using TurnHub::IntentType;
using TurnHub::INVALID_ID;
using TurnHub::LedRenderer;
using TurnHub::MAX_CONTROLLERS;
using TurnHub::MAX_PHYSICAL_SIGILS;
using TurnHub::OtaManager;
using TurnHub::SigilBus;
using TurnHub::SigilEvent;
using TurnHubProtocol::PacketType;

// --- Runtime objects (defined in main.cpp) ----------------------------------

extern SigilBus sigilBus;
extern IntentDispatcher intents;
extern LedRenderer leds;
extern AudioController audio;
extern OtaManager ota;

extern bool espNowReady;
// Atlas's pairing window; admins choose it (pairing_settings.h).
extern uint32_t pairingWindowMs;
// Screen off and auto sleep (power_settings.h); changed only by
// handleConfigurePowerIntent.
extern TurnHub::PowerSettings powerSettings;
// How far this Atlas is through first-run setup (setup_stage.h). Loaded at
// boot by beginFirstRunSetup(); changed only by handleAdvanceSetupIntent.
extern TurnHub::SetupStage setupStage;
void beginFirstRunSetup();

// --- The hosted game (hosted_game.h) ------------------------------------------

// A hub's game has started and isn't over (Starting, Running or Paused).
bool gameUnderway(uint32_t nowMs);
// Between games: OTA, Sigil updates, pairing, forgetting Sigils and sleep wait for this.
bool betweenGames(uint32_t nowMs);
// A player of the hub's live game sits on this Sigil.
bool sigilInGame(uint8_t sigilId, uint32_t nowMs);

// --- main.cpp ----------------------------------------------------------------

bool configureIntentHandlers();
void observeIntent(const Intent &intent);
// GET /api/v1/state: Atlas's own state (no game; protocol/http-v1.md).
uint32_t clientRevision();
String clientSnapshot(const String &atlasId, const char *bootId);
void handleStatus();
void startNetworking();

// --- device_intents.cpp ------------------------------------------------------

IntentResult handlePairRequestIntent(const Intent &intent, void *);
IntentResult handleForgetPairingIntent(const Intent &intent, void *);
IntentResult handlePairConfirmIntent(const Intent &intent, void *);
IntentResult handlePhonePairingIntent(const Intent &intent, void *);
IntentResult handleConfigurePairingIntent(const Intent &intent, void *);
IntentResult handleConfigureSpeakerIntent(const Intent &intent, void *);
IntentResult handleConfigurePowerIntent(const Intent &intent, void *);
IntentResult handleConfigureUploadApprovalIntent(const Intent &intent, void *);
IntentResult handleConfigureTableCodeIntent(const Intent &intent, void *);
// Return to lobby: ends the hub's hold on the table (it keeps its game).
IntentResult handleResetTableIntent(const Intent &intent, void *);
IntentResult handleFactoryResetIntent(const Intent &intent, void *);
IntentResult handleAdvanceSetupIntent(const Intent &intent, void *);
// Runs a scheduled Atlas factory reset once the web reply has had time to
// leave (called from loop()). eraseSettingsAndRestart() is firmware-only
// (factory_reset.cpp); host tests stub it.
void serviceFactoryReset(uint32_t nowMs);
bool factoryResetScheduled();
void eraseSettingsAndRestart();
// Menu > Device Sleep (owner 2026-10-02): scheduled by handleSleepIntent, run
// from loop() once the notice has been on screen. sleepAtlas() is firmware-
// only (atlas_display.h); host tests stub it. Waking is a restart: saved data
// survives, sessions do not.
// The same Intent with System origin is auto sleep on the cell
// (serviceAutoSleep): payload.value SLEEP_REASON_IDLE between games, or
// SLEEP_REASON_EMPTY at any time.
IntentResult handleSleepIntent(const Intent &intent, void *);
void serviceSleep(uint32_t nowMs);
bool sleepScheduled();
constexpr int32_t SLEEP_REASON_MENU = 0;
constexpr int32_t SLEEP_REASON_IDLE = 1;
constexpr int32_t SLEEP_REASON_EMPTY = 2;
// On the cell only: dispatches Sleep after the auto-sleep choice
// (power_settings.h; Off never) with no player action between games, or once the charge has read empty for
// BATTERY_EMPTY_CONFIRM_MS. Called from loop().
void serviceAutoSleep(uint32_t nowMs);
// A player did something (a non-System Intent, a touch, a hub's push):
// restarts the idle count.
void noteAtlasActivity(uint32_t nowMs);

// --- sigil_input.cpp ---------------------------------------------------------

void processSigilEvents();

// --- web_adapters.cpp --------------------------------------------------------

// Registers every browser adapter with TurnHubWebApi.
void registerWebCallbacks();
bool manageDevices(const String &actor, IntentType type, int32_t value, String &message);

// --- front_panel.cpp ---------------------------------------------------------

// True while the pairing window is open. Exposed for host tests.
extern bool pairingActive;

void beginFrontPanel();
void startPairingIndicator(uint32_t nowMs);
// Touchscreen Device screen (Menu, between games; owner 2026-10-02): the
// same holds as the BOOT button's gestures, which these buttons stand in for
// when BOOT is out of reach inside a case.
constexpr uint32_t DEVICE_UNPAIR_HOLD_MS = TurnHubProtocol::UNPAIR_HOLD_MS;
constexpr uint32_t DEVICE_RESET_HOLD_MS = TurnHubProtocol::FACTORY_RESET_HOLD_MS;
// Proof that someone is at the table (owner decision 2026-09-25; replaced the
// 3 s "Unlock admin" hold). A signed-in phone asks for a code, the Atlas
// screen shows it (digits and a QR code), and entering it on that phone
// verifies that profile for PRESENCE_GRANT_MS. Protected web actions (first
// Admin, system settings, device names, OTA, Return to lobby, factory reset)
// need the account permission too. Authorization state only, never game
// state, so it lives outside the Intent path like the old unlock window.
constexpr uint32_t PRESENCE_CODE_MS = 90000;      // How long a shown code works.
constexpr uint32_t PRESENCE_GRANT_MS = 600000;    // How long a verified profile stays verified.
constexpr uint8_t PRESENCE_MAX_ATTEMPTS = 5;      // Wrong codes before the code is canceled.
constexpr uint8_t PRESENCE_MAX_GRANTS = 4;
struct PresenceRequest {
  char profileId[9] = {};
  uint32_t code = 0;        // Six digits.
  uint32_t shownAtMs = 0;
  bool setup = false;       // No Admin exists yet: this verifies first-Admin setup.
};
enum class PresenceOutcome : uint8_t { Verified, WrongCode, NoCode, TooManyAttempts };
// Shows a new code on the Atlas screen for this profile, replacing an older
// request (one at a time, so nobody can queue codes up).
bool requestPresenceCode(const String &profileId, bool setup, uint32_t nowMs);
PresenceOutcome confirmPresenceCode(const String &profileId, uint32_t code, uint32_t nowMs);
// The code on screen now, or nullptr.
const PresenceRequest *pendingPresenceCode(uint32_t nowMs);
void cancelPresenceCode();
// Admin setting (table_code_setting.h, off by default): while false, every
// signed-in profile counts as verified at the table and no code is needed.
extern bool tableCodeRequired;
extern bool uploadApprovalRequired;
bool presenceConfirmedFor(const String &profileId, uint32_t nowMs);
// Time left for that profile, or 0.
uint32_t presenceRemainingMs(const String &profileId, uint32_t nowMs);
// Time left on a code that profile actually entered, whatever the setting.
uint32_t presenceCodeRemainingMs(const String &profileId, uint32_t nowMs);
// Someone verified is at the table (for the Wi-Fi QR code's private password).
bool anyPresenceActive(uint32_t nowMs);
void revokePresence(const String &profileId);
void resetPresence();
// OTA is allowed only between games (the upload also needs a verified Admin).
bool otaAllowed();
void updatePairingWindow(uint32_t nowMs);
// How long "identify" on the serial console blinks the on-board LED white.
constexpr uint32_t IDENTIFY_MS = 10000;
void readSerialCommands(uint32_t nowMs);
// Adapter for the board's BOOT button (raw level, debounced here): quick press
// = Pair a Sigil, hold UNPAIR_HOLD_MS = forget all Sigils, hold
// FACTORY_RESET_HOLD_MS = factory reset Atlas. Each becomes an Intent from
// IntentOrigin::AtlasHardware, like the touchscreen's buttons.
void updateBootButton(bool pressed, uint32_t nowMs);
// Time left in the open pairing window, or 0 when it is closed.
uint32_t pairingRemainingMs(uint32_t nowMs);
// Newer firmware (update_notice.h): an app reports the newest release, and
// while Atlas or any paired Sigil runs something older the on-board LED
// blinks blue (pairing's red blink comes first) and the screen says so.
void noteLatestFirmware(const TurnHub::LatestFirmware &latest);
// The last report, or nullptr since boot.
const TurnHub::LatestFirmware *latestFirmware();
// Devices (Atlas, and paired Sigils that have said their version) running
// older firmware than the last report. Test harness boards don't count.
uint8_t firmwareUpdatesAvailable();
// Who is behind, for the screen and every Sigil's notice (UpdateKind).
TurnHub::UpdateKind firmwareUpdateKind();

}  // namespace TurnHubAtlas
