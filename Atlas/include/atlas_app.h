#pragma once

// Atlas application layer: the runtime objects, table-decision state and
// helpers shared by the Intent handlers and the transport adapters.
//
// Module layout (everything below lives in namespace TurnHubAtlas):
//
//   main.cpp               runtime object definitions, handler bindings,
//                          networking/HTTP bring-up, setup() and loop()
//   app_context.cpp        seat resolution, audio masks, Intent builders
//   gameplay_intents.cpp   PASS, pause/resume, concede, win claims,
//                          life/Commander counters, turn-timer cues
//   table_intents.cpp      lobby participation, starter selection, start
//                          countdown, rematch/reset, elimination, pairing,
//                          game settings
//   moderation_intent.cpp  Game Master moderation
//   sigil_input.cpp        ESP-NOW event adapter and physical gesture state
//   web_adapters.cpp       browser callbacks registered with TurnHubWebApi
//   front_panel.cpp        Atlas master/pair buttons and front-panel LEDs
//   sigil_accessibility.cpp  seated players' accessibility preferences ->
//                          each Sigil's LED style, sound and hold timing
//
// Ownership rule (ARCHITECTURAL_INVARIANTS.md, Invariants 2 and 10):
// handle*Intent functions are the only code that validates and mutates
// canonical table state. Adapters build an Intent and dispatch it;
// tests/host/audit_adapters.py fails the build if one bypasses the dispatcher.

#include <Arduino.h>
#include <WebServer.h>

#include "accessibility_prefs.h"
#include "audio_controller.h"
#include "client_state.h"
#include "game_engine.h"
#include "intent.h"
#include "intent_dispatcher.h"
#include "led_renderer.h"
#include "lobby.h"
#include "ota_manager.h"
#include "protocol.h"
#include "sigil_bus.h"
#include "turnhub_types.h"
#include "web_api.h"

// The portal/API server is shared with OtaManager and the host test harness.
extern WebServer server;

namespace TurnHubAtlas {

using TurnHub::AudioController;
using TurnHub::GameEngine;
using TurnHub::HubState;
using TurnHub::Intent;
using TurnHub::IntentDispatcher;
using TurnHub::IntentOrigin;
using TurnHub::IntentResult;
using TurnHub::IntentStatus;
using TurnHub::IntentType;
using TurnHub::INVALID_ID;
using TurnHub::LedRenderer;
using TurnHub::Lobby;
using TurnHub::MAX_CONTROLLERS;
using TurnHub::MAX_PHYSICAL_SIGILS;
using TurnHub::MAX_PLAYERS;
using TurnHub::OtaManager;
using TurnHub::PlayerSeat;
using TurnHub::SigilBus;
using TurnHub::SigilEvent;
using TurnHub::stateName;
using TurnHubProtocol::PacketType;
using TurnHubWebApi::SeatSnapshot;
using TurnHubWebApi::WebControl;

// --- Timing -----------------------------------------------------------------

constexpr uint32_t START_COUNTDOWN_MS = 3000;
// A queued PASS commits after this grace period unless it is cancelled.
// Keep in sync with the "within 3 seconds" wording in handlePassIntent.
constexpr uint32_t PASS_GRACE_MS = 3000;

// --- Runtime objects (defined in main.cpp) ----------------------------------

extern SigilBus sigilBus;
extern Lobby lobby;
extern GameEngine game;
extern IntentDispatcher intents;
extern LedRenderer leds;
extern AudioController audio;
extern OtaManager ota;
extern TurnHub::ClientState clientState;

// Settings the next match will start with; persisted by game_settings_store.
extern TurnHub::GameSettings nextGameSettings;
// False when the settings store failed to load; starting a game is refused.
extern bool gameSettingsAvailable;
extern bool espNowReady;

// --- Canonical table-decision state (mutated only by Intent handlers) -------

struct PendingPassState {
  bool active = false;
  PlayerSeat seat;
  uint32_t requestedAtMs = 0;
  IntentOrigin origin = IntentOrigin::Unknown;
};

extern HubState hubState;
extern PendingPassState pendingPass;
extern uint32_t countdownStartedAtMs;
extern int8_t lastCountdownSecond;
// Player selected for elimination while paused; 0 when none.
extern uint8_t eliminationTargetPlayer;
// Controller/player that armed a win claim with the long-press pause gesture.
extern uint8_t winArmedModule;
extern uint8_t winArmedPlayer;

// Turn-timer cue tracking (see updateTurnTimerCues). Exposed for host tests.
struct TurnTimerCueState {
  uint8_t player = 0;
  uint32_t turnsCompleted = 0;
  TurnHub::TurnTimerPhase phase = TurnHub::TurnTimerPhase::Normal;
};
extern TurnTimerCueState turnTimerCue;

// --- main.cpp ----------------------------------------------------------------

bool configureIntentHandlers();
void observeClientState();
void observeIntent(const Intent &intent);
uint32_t clientRevision();
String clientSnapshot(const String &atlasId, const char *bootId);
void handleStatus();
void startNetworking();

// --- app_context.cpp ---------------------------------------------------------

const char *intentOriginName(IntentOrigin origin);

// Sigils whose joined/in-game seats should hear table-wide cues.
uint16_t lobbyAudioMask();
uint16_t gameAudioMask();

// Seat lookups against the running game, or the lobby before a game starts.
bool seatForModuleSlot(uint8_t controllerId, uint8_t slot, PlayerSeat &seat);
bool firstLivingSeatForModule(uint8_t controllerId, PlayerSeat &seat);
// The canonical seat named by an Intent actor, or nullptr if the actor's
// controller/slot/player triple no longer agrees with the game.
const PlayerSeat *seatForIntentActor(const Intent &intent);
bool resolveProfileParticipant(const String &profileId, uint8_t &controllerId, uint8_t &slot);

// Intent builders. Each returns the dispatcher's result unchanged.
IntentResult dispatchSeatIntent(IntentType type, IntentOrigin origin,
    const PlayerSeat &seat, uint32_t flags = 0);
IntentResult dispatchModuleIntent(IntentType type, uint8_t controllerId,
    uint8_t slot = 1, int32_t value = 0);
IntentResult dispatchSystemIntent(IntentType type);

// --- gameplay_intents.cpp ----------------------------------------------------

IntentResult handlePassIntent(const Intent &intent, void *);
IntentResult handleCommitPassIntent(const Intent &intent, void *);
IntentResult handlePauseIntent(const Intent &intent, void *);
IntentResult handleResumeIntent(const Intent &intent, void *);
IntentResult handleTogglePauseIntent(const Intent &intent, void *);
IntentResult handleConcedeIntent(const Intent &intent, void *);
IntentResult handleClaimWinIntent(const Intent &intent, void *);
IntentResult handleConfirmWinIntent(const Intent &intent, void *);
IntentResult handleDenyWinIntent(const Intent &intent, void *);
IntentResult handleChangeLifeIntent(const Intent &intent, void *);
IntentResult handleExpireLifeChangesIntent(const Intent &intent, void *);
IntentResult handleCounterIntent(const Intent &intent, void *);

void clearPendingPass(const char *reason);
bool cancelPendingPassForModule(uint8_t sigilId, const char *reason);
// Loop ticks: commit an expired PASS grace period; emit timer phase cues.
void updatePendingPass(uint32_t nowMs);
void updateTurnTimerCues(uint32_t nowMs);

// --- table_intents.cpp -------------------------------------------------------

IntentResult handleProfileParticipationIntent(const Intent &intent, void *);
IntentResult handleSeatMembershipIntent(const Intent &intent, void *);
IntentResult handleSelectStarterIntent(const Intent &intent, void *);
IntentResult handleStartIntent(const Intent &intent, void *);
IntentResult handleCompleteStartIntent(const Intent &intent, void *);
IntentResult handleCancelStartIntent(const Intent &intent, void *);
IntentResult handleResetIntent(const Intent &intent, void *);
IntentResult handleCancelPassIntent(const Intent &intent, void *);
IntentResult handleEliminationIntent(const Intent &intent, void *);
IntentResult handlePairRequestIntent(const Intent &intent, void *);
IntentResult handleGameSettingsIntent(const Intent &intent, void *);

// Clears Atlas-owned decisions and the physical gesture bookkeeping.
void clearDecisionState();
void enterEmptyLobby(const Intent *cause = nullptr);
// Enters GameOver after the engine reports a finished match.
void finishGameState();
// Loop tick: completes the start countdown through a System intent.
void updateCountdown(uint32_t nowMs);

// --- moderation_intent.cpp ---------------------------------------------------

IntentResult handleModerateIntent(const Intent &intent, void *);

// --- sigil_input.cpp ---------------------------------------------------------

void processSigilEvents();
void handlePass(uint8_t sigilId);
void handleActionDown(uint8_t sigilId);
void handleActionUp(uint8_t sigilId);
void handleActionShort(uint8_t sigilId);
void handleActionLong(uint8_t sigilId);
void handleActionWin(uint8_t sigilId);
// Loop tick: expires the post-cancel Action suppression window.
void updateActionCancelSuppression(uint32_t nowMs);
// Forgets held/chord/suppression bookkeeping for every physical Sigil.
void resetGestureState();

// --- sigil_accessibility.cpp -------------------------------------------------

// The merged preferences of the players seated at (or last bound to) a Sigil.
TurnHubProfiles::AccessibilityPrefs seatedAccessibility(uint8_t sigilId);
// Restyle one Sigil / every Sigil now (after a preference or seat change).
void applySigilAccessibility(uint8_t sigilId, uint32_t nowMs);
void applyAllSigilAccessibility(uint32_t nowMs);
// Loop tick: revisits one Sigil per tick and resends hold timing.
void updateSigilAccessibility(uint32_t nowMs);

// --- web_adapters.cpp --------------------------------------------------------

// Registers every browser adapter with TurnHubWebApi.
void registerWebCallbacks();
bool resolveWebSeat(uint8_t controllerId, uint8_t slot, SeatSnapshot &snapshot);
bool handleWebControl(uint8_t controllerId, uint8_t slot, WebControl control, String &message);
bool handleProfileControl(const String &profileId, WebControl control,
    uint8_t controllerId, uint8_t slot, String &message);
bool readGameSettings(TurnHub::GameSettings &settings, bool &editable);
bool configureGame(uint8_t controller, uint8_t slot,
    const TurnHub::GameSettings &settings, String &message);
bool changeLife(uint8_t controller, uint8_t slot, int32_t delta, String &message);
bool readCounters(uint8_t controller, uint8_t slot, TurnHubWebApi::CounterSnapshot &snapshot);
bool changeCounter(uint8_t controller, uint8_t slot, IntentType type,
    const TurnHub::IntentPayload &payload, String &message);
bool moderateAccount(const String &actor, const String &target,
    const String &action, String &message);

// --- front_panel.cpp ---------------------------------------------------------

// True while the pairing window's Pair LED is blinking. Exposed for host tests.
extern bool pairingActive;

void beginFrontPanel();
// Starts the three-flash status blink once boot has finished.
void startBootBlink(uint32_t nowMs);
void startPairingIndicator(uint32_t nowMs);
// OTA is allowed only between games and while the master button is held.
bool otaAllowed();
void updateMasterButton();
void updatePairButton();
void updateFrontPanelLeds(uint32_t nowMs);

}  // namespace TurnHubAtlas
