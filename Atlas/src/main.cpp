#include <Arduino.h>
#include "optional_preferences.h"
#include <WebServer.h>
#include <WiFi.h>

#include "audio_controller.h"
#include "config.h"
#include "firmware_version.h"
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

WebServer server(AtlasConfig::HTTP_PORT);

namespace {

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

constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t START_COUNTDOWN_MS = 3000;
constexpr uint32_t PASS_GRACE_MS = 3000;
constexpr uint32_t ACTION_CANCEL_RELEASE_CLEAR_MS = 250;
constexpr uint32_t DEFAULT_WARNING_MS = 0;
constexpr uint8_t WIFI_PASSWORD_LENGTH = 16;
constexpr char WIFI_PREF_NAMESPACE[] = "atlas-net";
constexpr char WIFI_PREF_KEY[] = "ap-pass";
constexpr char WIFI_PASSWORD_CHARS[] =
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

bool otaAllowed();

SigilBus sigilBus(AtlasConfig::WIFI_CHANNEL);
Lobby lobby;
GameEngine game;
IntentDispatcher intents;
LedRenderer leds(sigilBus);
AudioController audio(sigilBus);
OtaManager ota(server, otaAllowed);

HubState hubState = HubState::Lobby;
bool espNowReady = false;
bool lastButtonState = HIGH;
uint32_t lastDebounceMs = 0;
bool lastPairButtonState = HIGH;
uint32_t lastPairDebounceMs = 0;
uint32_t countdownStartedAtMs = 0;
int8_t lastCountdownSecond = -1;

uint8_t eliminationTargetPlayer = 0;
bool eliminationChord[MAX_PHYSICAL_SIGILS] = {};
bool suppressEliminationShort[MAX_PHYSICAL_SIGILS] = {};
uint8_t winArmedModule = INVALID_ID;
uint8_t winArmedPlayer = 0;

struct PendingPassState {
  bool active = false;
  PlayerSeat seat;
  uint32_t requestedAtMs = 0;
  IntentOrigin origin = IntentOrigin::Unknown;
};

enum class PassRequestResult : uint8_t {
  Rejected,
  Armed,
  Cancelled,
};

PendingPassState pendingPass;
bool suppressActionAfterPassCancel[MAX_PHYSICAL_SIGILS] = {};
uint32_t suppressActionReleasedAtMs[MAX_PHYSICAL_SIGILS] = {};

const char *intentOriginName(IntentOrigin origin) {
  switch (origin) {
    case IntentOrigin::PhysicalSigil: return "SIGIL";
    case IntentOrigin::Browser: return "BROWSER";
    case IntentOrigin::AndroidApp: return "ANDROID";
    case IntentOrigin::AtlasHardware: return "ATLAS";
    case IntentOrigin::Simulator: return "SIMULATOR";
    case IntentOrigin::System: return "SYSTEM";
    case IntentOrigin::Unknown:
    default:
      return "UNKNOWN";
  }
}

String generateWifiPassword() {
  String password;
  password.reserve(WIFI_PASSWORD_LENGTH);
  constexpr size_t charCount = sizeof(WIFI_PASSWORD_CHARS) - 1;
  for (uint8_t i = 0; i < WIFI_PASSWORD_LENGTH; ++i) {
    password += WIFI_PASSWORD_CHARS[esp_random() % charCount];
  }
  return password;
}

String loadOrCreateWifiPassword() {
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(WIFI_PREF_NAMESPACE, false)) {
    Serial.println("ATLAS|WIFI_AP|PASSWORD_STORE|ERROR");
    return generateWifiPassword();
  }

  String password = prefs.getString(WIFI_PREF_KEY, "");
  if (password.length() < 8 || password.length() > 63) {
    password = generateWifiPassword();
    if (prefs.putString(WIFI_PREF_KEY, password) == 0) {
      Serial.println("ATLAS|WIFI_AP|PASSWORD_STORE|WRITE_ERROR");
    } else {
      Serial.println("ATLAS|WIFI_AP|PASSWORD_STORE|GENERATED");
    }
  } else {
    Serial.println("ATLAS|WIFI_AP|PASSWORD_STORE|LOADED");
  }

  prefs.end();
  return password;
}

bool masterButtonPressed() {
  return digitalRead(AtlasConfig::MASTER_BUTTON_PIN) == LOW;
}

bool otaAllowed() {
  const bool safeState =
      hubState == HubState::Lobby ||
      hubState == HubState::GameOver;
  return safeState && masterButtonPressed();
}

uint16_t lobbyAudioMask() {
  uint16_t mask = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (lobby.isJoined(id)) {
      mask |= AudioController::maskForSigil(id);
    }
  }
  return mask;
}

uint16_t gameAudioMask() {
  uint16_t mask = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (game.moduleInGame(id)) {
      mask |= AudioController::maskForSigil(id);
    }
  }
  return mask;
}

void printPlayer(const PlayerSeat &player) {
  Serial.print("Player ");
  Serial.print(player.playerNumber);
  Serial.print(" / Sigil ");
  Serial.print(player.moduleId);
  Serial.print(player.slotName());
}

bool seatForModuleSlot(uint8_t moduleId, uint8_t slot, PlayerSeat &seat) {
  PlayerSeat local[2];
  const uint8_t count = game.hasPlayers()
      ? game.playersForModule(moduleId, local, 2)
      : lobby.playersForModule(moduleId, local, 2);

  for (uint8_t i = 0; i < count; ++i) {
    if (local[i].slot == slot) {
      seat = local[i];
      return true;
    }
  }
  return false;
}

bool firstLivingSeatForModule(uint8_t moduleId, PlayerSeat &seat) {
  PlayerSeat local[2];
  const uint8_t count = game.livingPlayersForModule(moduleId, local, 2);
  if (count == 0) {
    return false;
  }
  seat = local[0];
  return true;
}

const PlayerSeat *seatForIntentActor(const Intent &intent) {
  if (intent.actor.playerNumber == 0) {
    return nullptr;
  }

  const PlayerSeat *seat = game.playerByNumber(intent.actor.playerNumber);
  if (
      seat == nullptr ||
      seat->moduleId != intent.actor.moduleId ||
      seat->slot != intent.actor.slot) {
    return nullptr;
  }
  return seat;
}

bool resolveWebSeat(
    uint8_t moduleId,
    uint8_t slot,
    SeatSnapshot &snapshot) {
  PlayerSeat seat;
  if (!seatForModuleSlot(moduleId, slot, seat)) {
    snapshot = SeatSnapshot{};
    return false;
  }

  snapshot.exists = true;
  snapshot.playerNumber = seat.playerNumber;
  snapshot.active = false;
  snapshot.eliminated = false;

  if (game.hasPlayers()) {
    snapshot.eliminated = game.isEliminated(seat.playerNumber);
    const PlayerSeat *active = game.activePlayer();
    snapshot.active = active != nullptr && active->sameSeat(seat);
  }

  return true;
}

void clearPendingPass(const char *reason) {
  if (!pendingPass.active) {
    return;
  }

  Serial.print("ATLAS|GAME|PASS|CANCEL|PLAYER|");
  Serial.print(pendingPass.seat.playerNumber);
  Serial.print("|ORIGIN|");
  Serial.print(intentOriginName(pendingPass.origin));
  if (reason != nullptr && reason[0] != '\0') {
    Serial.print("|REASON|");
    Serial.print(reason);
  }
  Serial.println();

  pendingPass = PendingPassState{};
  leds.invalidateAll();
}

bool cancelPendingPassForModule(uint8_t sigilId, const char *reason) {
  if (!pendingPass.active || pendingPass.seat.moduleId != sigilId) {
    return false;
  }
  clearPendingPass(reason);
  return true;
}

PassRequestResult requestPass(
    const PlayerSeat &seat,
    uint32_t nowMs,
    IntentOrigin origin) {
  if (hubState != HubState::Running) {
    return PassRequestResult::Rejected;
  }

  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || !active->sameSeat(seat)) {
    return PassRequestResult::Rejected;
  }

  if (pendingPass.active) {
    if (pendingPass.seat.sameSeat(seat)) {
      clearPendingPass("PASS");
      return PassRequestResult::Cancelled;
    }
    return PassRequestResult::Rejected;
  }

  pendingPass.active = true;
  pendingPass.seat = seat;
  pendingPass.requestedAtMs = nowMs;
  pendingPass.origin = origin;

  Serial.print("ATLAS|GAME|PASS|PENDING|PLAYER|");
  Serial.print(seat.playerNumber);
  Serial.print("|ORIGIN|");
  Serial.print(intentOriginName(origin));
  Serial.print("|GRACE_MS|");
  Serial.println(PASS_GRACE_MS);
  leds.invalidateAll();
  return PassRequestResult::Armed;
}

IntentResult handlePassIntent(const Intent &intent, void *) {
  if (hubState != HubState::Running) {
    return IntentResult::reject(
        IntentStatus::InvalidState,
        "Pass is only available during a running game");
  }

  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr) {
    return IntentResult::reject(
        IntentStatus::InvalidState,
        "There is no active player");
  }

  if (
      intent.actor.playerNumber != active->playerNumber ||
      intent.actor.moduleId != active->moduleId ||
      intent.actor.slot != active->slot) {
    return IntentResult::reject(
        IntentStatus::Unauthorized,
        "It is not this seat's turn");
  }

  Serial.print("ATLAS|INTENT|PASS|ORIGIN|");
  Serial.print(intentOriginName(intent.actor.origin));
  Serial.print("|PLAYER|");
  Serial.println(active->playerNumber);

  const PassRequestResult result = requestPass(
      *active,
      millis(),
      intent.actor.origin);

  if (result == PassRequestResult::Armed) {
    return IntentResult::accept(
        "Pass queued. Press Pass or Action within 3 seconds to cancel.");
  }
  if (result == PassRequestResult::Cancelled) {
    return IntentResult::accept("Pending pass cancelled");
  }

  return IntentResult::reject(
      IntentStatus::Conflict,
      "Atlas rejected the pass");
}

IntentResult handlePauseIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (seat == nullptr || game.isEliminated(intent.actor.playerNumber)) {
    return IntentResult::reject(
        IntentStatus::InvalidActor,
        "This player is not active in the game");
  }

  if (hubState != HubState::Running) {
    return IntentResult::reject(
        IntentStatus::InvalidState,
        "Pause is only available during a running game");
  }

  clearPendingPass("PAUSE");
  const PlayerSeat *active = game.activePlayer();
  if (!game.pause(millis())) {
    return IntentResult::reject(
        IntentStatus::Conflict,
        "Could not pause the game");
  }

  hubState = HubState::Paused;
  if (
      intent.actor.origin == IntentOrigin::PhysicalSigil &&
      active != nullptr &&
      active->moduleId == intent.actor.moduleId) {
    winArmedModule = intent.actor.moduleId;
    winArmedPlayer = active->playerNumber;
  } else {
    winArmedModule = INVALID_ID;
    winArmedPlayer = 0;
  }

  audio.pause(gameAudioMask());
  leds.invalidateAll();
  Serial.print("ATLAS|INTENT|PAUSE|ORIGIN|");
  Serial.print(intentOriginName(intent.actor.origin));
  Serial.print("|PLAYER|");
  Serial.println(seat->playerNumber);
  return IntentResult::accept("Game paused");
}

IntentResult handleResumeIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (seat == nullptr || game.isEliminated(intent.actor.playerNumber)) {
    return IntentResult::reject(
        IntentStatus::InvalidActor,
        "This player is not active in the game");
  }

  if (hubState != HubState::Paused) {
    return IntentResult::reject(
        IntentStatus::InvalidState,
        "Resume is only available while the game is paused");
  }
  if (game.hasWinClaim()) {
    return IntentResult::reject(
        IntentStatus::Conflict,
        "Resolve the win claim before resuming");
  }
  if (eliminationTargetPlayer != 0) {
    return IntentResult::reject(
        IntentStatus::Conflict,
        "Resolve the elimination before resuming");
  }
  if (!game.resume(millis())) {
    return IntentResult::reject(
        IntentStatus::Conflict,
        "Could not resume the game");
  }

  hubState = HubState::Running;
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  audio.resume(gameAudioMask());
  leds.invalidateAll();
  Serial.print("ATLAS|INTENT|RESUME|ORIGIN|");
  Serial.print(intentOriginName(intent.actor.origin));
  Serial.print("|PLAYER|");
  Serial.println(seat->playerNumber);
  return IntentResult::accept("Game resumed");
}

IntentResult handleConcedeIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (seat == nullptr) {
    return IntentResult::reject(
        IntentStatus::InvalidActor,
        "This seat is no longer at the table");
  }
  if (hubState != HubState::Running && hubState != HubState::Paused) {
    return IntentResult::reject(
        IntentStatus::InvalidState,
        "Concede is only available during an active game");
  }
  if (game.hasWinClaim() || eliminationTargetPlayer != 0) {
    return IntentResult::reject(
        IntentStatus::Conflict,
        "Resolve the current table decision first");
  }
  if (game.isEliminated(seat->playerNumber)) {
    return IntentResult::reject(
        IntentStatus::Conflict,
        "This player has already left the game");
  }

  clearPendingPass("CONCEDE");
  const uint32_t nowMs = millis();
  const bool restoreRunning = hubState == HubState::Running;
  if (restoreRunning) {
    if (!game.pause(nowMs)) {
      return IntentResult::reject(
          IntentStatus::Conflict,
          "Could not prepare the concession");
    }
    hubState = HubState::Paused;
  }

  bool gameFinished = false;
  if (!game.eliminatePlayer(
          seat->playerNumber,
          DEFAULT_WARNING_MS,
          nowMs,
          gameFinished)) {
    if (restoreRunning && game.resume(nowMs)) {
      hubState = HubState::Running;
    }
    return IntentResult::reject(
        IntentStatus::Conflict,
        "Atlas rejected the concession");
  }

  audio.playerEliminated(seat->moduleId);
  leds.invalidateAll();
  Serial.print("ATLAS|INTENT|CONCEDE|ORIGIN|");
  Serial.print(intentOriginName(intent.actor.origin));
  Serial.print("|PLAYER|");
  Serial.println(seat->playerNumber);

  if (gameFinished) {
    finishGameState();
  } else if (restoreRunning && game.resume(nowMs)) {
    hubState = HubState::Running;
  }

  return IntentResult::accept("Player conceded");
}

IntentResult handleClaimWinIntent(const Intent &intent, void *) {
  const PlayerSeat *resolved = seatForIntentActor(intent);
  if (resolved == nullptr) {
    return IntentResult::reject(IntentStatus::InvalidActor, "This seat is no longer at the table");
  }
  const PlayerSeat seat = *resolved;
  const uint32_t nowMs = millis();
  if (hubState != HubState::Running && hubState != HubState::Paused) {
    return IntentResult::reject(IntentStatus::InvalidState, "Win claim is unavailable right now");
  }
  if (game.hasWinClaim() || eliminationTargetPlayer != 0) {
    return IntentResult::reject(IntentStatus::InvalidState, "Another table decision is already pending");
  }
  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || !active->sameSeat(seat) ||
      game.isEliminated(seat.playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Only the active player can claim a win");
  }

  const bool armedClaim = (intent.payload.flags & TurnHub::CLAIM_FROM_ARMED_PAUSE) != 0;
  if (armedClaim && (hubState != HubState::Paused ||
      winArmedModule != seat.moduleId || winArmedPlayer != seat.playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Win claim is not armed");
  }
  clearPendingPass("WIN_CLAIM");
  const bool restoreRunning = hubState == HubState::Running || armedClaim;
  if (!game.beginWinClaim(seat.playerNumber, restoreRunning, nowMs)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not start the win claim");
  }
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  leds.invalidateAll();

  if (game.gameOver()) {
    finishGameState();
  } else {
    hubState = HubState::Paused;
    audio.winClaimed(gameAudioMask());
  }

  Serial.print("ATLAS|INTENT|WIN|CLAIMED|PLAYER|");
  Serial.println(seat.playerNumber);
  return IntentResult::accept("Win claim sent to the table");
}

IntentResult handleConfirmWinIntent(const Intent &intent, void *) {
  const PlayerSeat *resolved = seatForIntentActor(intent);
  if (resolved == nullptr) {
    return IntentResult::reject(IntentStatus::InvalidActor, "This seat is no longer at the table");
  }
  const PlayerSeat seat = *resolved;
  const uint32_t nowMs = millis();
  if (hubState != HubState::Paused || !game.hasWinClaim()) {
    return IntentResult::reject(IntentStatus::InvalidState, "There is no win claim to confirm");
  }
  if (game.nextWinConfirmationPlayerNumber() != seat.playerNumber) {
    return IntentResult::reject(IntentStatus::InvalidState, "Another player must respond first");
  }

  bool gameFinished = false;
  if (!game.confirmWinClaim(seat.playerNumber, nowMs, gameFinished)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not confirm the win claim");
  }
  audio.winConfirmed(gameAudioMask());
  leds.invalidateAll();
  Serial.print("ATLAS|INTENT|WIN|CONFIRMED|PLAYER|");
  Serial.println(seat.playerNumber);
  if (gameFinished) {
    finishGameState();
  }
  return IntentResult::accept("Win claim confirmed");
}

IntentResult handleDenyWinIntent(const Intent &intent, void *) {
  const PlayerSeat *resolved = seatForIntentActor(intent);
  if (resolved == nullptr) {
    return IntentResult::reject(IntentStatus::InvalidActor, "This seat is no longer at the table");
  }
  const PlayerSeat seat = *resolved;
  const uint32_t nowMs = millis();
  if (hubState != HubState::Paused || !game.hasWinClaim()) {
    return IntentResult::reject(IntentStatus::InvalidState, "There is no win claim to deny");
  }
  if (game.nextWinConfirmationPlayerNumber() != seat.playerNumber) {
    return IntentResult::reject(IntentStatus::InvalidState, "Another player must respond first");
  }
  if (!game.denyWinClaim(seat.playerNumber, nowMs)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not deny the win claim");
  }
  hubState = game.paused() ? HubState::Paused : HubState::Running;
  audio.winDenied(gameAudioMask());
  leds.invalidateAll();
  Serial.print("ATLAS|INTENT|WIN|DENIED|PLAYER|");
  Serial.println(seat.playerNumber);
  return IntentResult::accept("Win claim denied");
}

IntentResult dispatchSeatIntent(
    IntentType type,
    IntentOrigin origin,
    const PlayerSeat &seat,
    uint32_t flags = 0) {
  Intent intent;
  intent.type = type;
  intent.payload.flags = flags;
  intent.actor.origin = origin;
  intent.actor.moduleId = seat.moduleId;
  intent.actor.slot = seat.slot;
  intent.actor.playerNumber = seat.playerNumber;
  return intents.dispatch(intent);
}

IntentResult dispatchPassIntent(IntentOrigin origin, const PlayerSeat &seat) {
  return dispatchSeatIntent(IntentType::Pass, origin, seat);
}

bool configureIntentHandlers() {
  const bool passBound = intents.bind(IntentType::Pass, handlePassIntent);
  const bool pauseBound = intents.bind(IntentType::Pause, handlePauseIntent);
  const bool resumeBound = intents.bind(IntentType::Resume, handleResumeIntent);
  const bool concedeBound = intents.bind(IntentType::Concede, handleConcedeIntent);

  Serial.println(passBound
                     ? "ATLAS|INTENT|PASS|BOUND"
                     : "ATLAS|INTENT|PASS|BIND_FAILED");
  Serial.println(pauseBound
                     ? "ATLAS|INTENT|PAUSE|BOUND"
                     : "ATLAS|INTENT|PAUSE|BIND_FAILED");
  Serial.println(resumeBound
                     ? "ATLAS|INTENT|RESUME|BOUND"
                     : "ATLAS|INTENT|RESUME|BIND_FAILED");
  Serial.println(concedeBound
                     ? "ATLAS|INTENT|CONCEDE|BOUND"
                     : "ATLAS|INTENT|CONCEDE|BIND_FAILED");

  bool remainingBound = true;
  const struct { IntentType type; IntentDispatcher::Handler handler; } bindings[] = {
      {IntentType::ClaimWin, handleClaimWinIntent},
      {IntentType::ConfirmWin, handleConfirmWinIntent},
      {IntentType::DenyWin, handleDenyWinIntent},
  };
  for (const auto &binding : bindings) {
    const bool bound = intents.bind(binding.type, binding.handler);
    Serial.print("ATLAS|INTENT|");
    Serial.print(TurnHub::intentName(binding.type));
    Serial.println(bound ? "|BOUND" : "|BIND_FAILED");
    remainingBound = bound && remainingBound;
  }
  return passBound && pauseBound && resumeBound && concedeBound && remainingBound;
}

void updatePendingPass(uint32_t nowMs) {
  if (!pendingPass.active) {
    return;
  }

  const PlayerSeat *active = game.activePlayer();
  if (
      hubState != HubState::Running ||
      active == nullptr ||
      !active->sameSeat(pendingPass.seat)) {
    clearPendingPass("STATE_CHANGE");
    return;
  }

  if (nowMs - pendingPass.requestedAtMs < PASS_GRACE_MS) {
    return;
  }

  const PendingPassState committing = pendingPass;
  pendingPass = PendingPassState{};

  if (!game.passTurn(
          committing.seat.moduleId,
          DEFAULT_WARNING_MS,
          committing.requestedAtMs)) {
    Serial.print("ATLAS|GAME|PASS|COMMIT_REJECTED|PLAYER|");
    Serial.print(committing.seat.playerNumber);
    Serial.print("|ORIGIN|");
    Serial.println(intentOriginName(committing.origin));
    leds.invalidateAll();
    return;
  }

  const PlayerSeat *current = game.activePlayer();
  Serial.print("ATLAS|GAME|PASS|COMMIT|");
  Serial.print(committing.seat.playerNumber);
  Serial.print("->");
  Serial.print(current != nullptr ? current->playerNumber : 0);
  Serial.print("|ORIGIN|");
  Serial.println(intentOriginName(committing.origin));

  if (current != nullptr) {
    if (committing.seat.moduleId == current->moduleId) {
      audio.sameModulePass(current->moduleId);
    } else {
      audio.turnPass(current->moduleId);
    }
  }
  leds.invalidateAll();
}

void updateActionCancelSuppression(uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (
        suppressActionAfterPassCancel[id] &&
        suppressActionReleasedAtMs[id] != 0 &&
        nowMs - suppressActionReleasedAtMs[id] >=
            ACTION_CANCEL_RELEASE_CLEAR_MS) {
      suppressActionAfterPassCancel[id] = false;
      suppressActionReleasedAtMs[id] = 0;
    }
  }
}

void clearDecisionState() {
  eliminationTargetPlayer = 0;
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  pendingPass = PendingPassState{};
  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
    eliminationChord[i] = false;
    suppressEliminationShort[i] = false;
    suppressActionAfterPassCancel[i] = false;
    suppressActionReleasedAtMs[i] = 0;
  }
}

void enterEmptyLobby() {
  hubState = HubState::Lobby;
  lobby.resetEmpty();
  game.reset();
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
  clearDecisionState();
  audio.clear();
  leds.invalidateAll();
  Serial.println("ATLAS|LOBBY|EMPTY");
}

void enterRematchLobby() {
  hubState = HubState::Lobby;
  lobby.resetForRematch();
  game.reset();
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
  clearDecisionState();
  audio.clear();
  leds.invalidateAll();
  Serial.println("ATLAS|LOBBY|REMATCH");
}

void finishGameState() {
  clearPendingPass("GAME_OVER");
  hubState = HubState::GameOver;
  eliminationTargetPlayer = 0;
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  leds.invalidateAll();
  audio.gameOver(gameAudioMask());

  Serial.print("ATLAS|GAME|OVER|WINNER|");
  Serial.println(game.winnerPlayerNumber());
}

void beginCountdown() {
  if (lobby.playerCount() < 2) {
    return;
  }

  hubState = HubState::Starting;
  countdownStartedAtMs = millis();
  lastCountdownSecond = -1;
  lobby.clearStartArm();
  Serial.println("ATLAS|LOBBY|COUNTDOWN|START");
}

void cancelCountdown() {
  if (hubState != HubState::Starting) {
    return;
  }

  const uint16_t targets = lobbyAudioMask();
  hubState = HubState::Lobby;
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
  lobby.clearStartArm();
  audio.clear();
  audio.countdownCancelled(targets);
  Serial.println("ATLAS|LOBBY|COUNTDOWN|CANCEL");
}

void startGame() {
  PlayerSeat starter;
  if (!lobby.starterOrDefault(starter) || lobby.playerCount() < 2) {
    cancelCountdown();
    return;
  }

  PlayerSeat players[MAX_PLAYERS];
  const uint8_t count = lobby.buildPlayers(players, MAX_PLAYERS);

  if (!game.start(
          players,
          count,
          starter,
          DEFAULT_WARNING_MS,
          millis())) {
    cancelCountdown();
    return;
  }

  hubState = HubState::Running;
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
  clearDecisionState();
  leds.invalidateAll();
  audio.gameStart(gameAudioMask());

  Serial.print("ATLAS|GAME|START|");
  printPlayer(starter);
  Serial.println();
}

void updateCountdown(uint32_t nowMs) {
  if (hubState != HubState::Starting) {
    return;
  }

  const uint32_t elapsed = nowMs - countdownStartedAtMs;
  const int8_t second = static_cast<int8_t>(elapsed / 1000);

  if (second >= 0 && second < 3 && second != lastCountdownSecond) {
    lastCountdownSecond = second;
    Serial.print("ATLAS|LOBBY|COUNTDOWN|");
    Serial.println(3 - second);
    audio.countdownTone(lobbyAudioMask(), static_cast<uint8_t>(second));
  }

  if (elapsed >= START_COUNTDOWN_MS) {
    startGame();
  }
}

void handleLobbyShort(uint8_t sigilId) {
  if (!lobby.isJoined(sigilId)) {
    const uint8_t playerNumber = lobby.join(sigilId);
    if (playerNumber == 0) {
      return;
    }

    Serial.print("ATLAS|LOBBY|JOIN|SIGIL|");
    Serial.print(sigilId);
    Serial.print("|PLAYER|");
    Serial.println(playerNumber);

    if (sigilId == lobby.hostModule()) {
      Serial.print("ATLAS|LOBBY|HOST|");
      Serial.println(sigilId);
    }
    audio.playerJoined(sigilId);
    leds.invalidateAll();
    return;
  }

  PlayerSeat selected;
  if (lobby.selectStarter(sigilId, selected)) {
    Serial.print("ATLAS|LOBBY|STARTER|");
    Serial.print(selected.playerNumber);
    Serial.print("|SIGIL|");
    Serial.print(selected.moduleId);
    Serial.print("|SLOT|");
    Serial.println(selected.slotName());
    audio.starterSelected(selected.moduleId);
  }
}

void beginEliminationSelection(uint8_t sigilId) {
  if (hubState != HubState::Paused || game.hasWinClaim()) {
    return;
  }

  PlayerSeat candidates[2];
  const uint8_t count = game.livingPlayersForModule(sigilId, candidates, 2);
  if (count == 0) {
    return;
  }

  eliminationTargetPlayer = candidates[0].playerNumber;
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  audio.eliminationArmed(sigilId);
  leds.invalidateAll();

  Serial.print("ATLAS|GAME|ELIMINATION|ARMED|PLAYER|");
  Serial.println(eliminationTargetPlayer);
}

void cycleEliminationTarget(uint8_t sigilId) {
  const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
  if (target == nullptr || target->moduleId != sigilId) {
    return;
  }

  PlayerSeat candidates[2];
  const uint8_t count = game.livingPlayersForModule(sigilId, candidates, 2);
  if (count == 0) {
    eliminationTargetPlayer = 0;
    return;
  }

  uint8_t nextIndex = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (candidates[i].playerNumber == eliminationTargetPlayer) {
      nextIndex = static_cast<uint8_t>((i + 1) % count);
      break;
    }
  }

  eliminationTargetPlayer = candidates[nextIndex].playerNumber;
  audio.eliminationTargetChanged(sigilId);
  leds.invalidateAll();

  Serial.print("ATLAS|GAME|ELIMINATION|TARGET|");
  Serial.println(eliminationTargetPlayer);
}

void cancelEliminationSelection() {
  const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
  if (target != nullptr) {
    audio.eliminationCancelled(target->moduleId);
  }
  eliminationTargetPlayer = 0;
  leds.invalidateAll();
  Serial.println("ATLAS|GAME|ELIMINATION|CANCEL");
}

void confirmElimination(uint8_t sigilId) {
  const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
  if (target == nullptr || target->moduleId != sigilId) {
    return;
  }

  const uint8_t eliminatedNumber = target->playerNumber;
  bool gameFinished = false;
  if (!game.eliminatePlayer(
          eliminatedNumber,
          DEFAULT_WARNING_MS,
          millis(),
          gameFinished)) {
    return;
  }

  eliminationTargetPlayer = 0;
  audio.playerEliminated(sigilId);
  leds.invalidateAll();

  Serial.print("ATLAS|GAME|ELIMINATED|PLAYER|");
  Serial.println(eliminatedNumber);

  if (gameFinished) {
    finishGameState();
  } else {
    hubState = HubState::Paused;
  }
}

bool handleWebControl(
    uint8_t moduleId,
    uint8_t slot,
    WebControl control,
    String &message) {
  PlayerSeat seat;
  if (!seatForModuleSlot(moduleId, slot, seat)) {
    message = "This seat is no longer at the table";
    return false;
  }

  switch (control) {
    case WebControl::SelectStarter: {
      if (hubState != HubState::Lobby) {
        message = "Starter can only be selected in the lobby";
        return false;
      }
      PlayerSeat selected;
      if (!lobby.selectStarterSeat(moduleId, slot, selected)) {
        message = "Could not select this seat as starter";
        return false;
      }
      audio.starterSelected(moduleId);
      leds.invalidateAll();
      Serial.print("ATLAS|WEB|STARTER|PLAYER|");
      Serial.println(selected.playerNumber);
      message = "Selected as starting player";
      return true;
    }

    case WebControl::Pass: {
      const IntentResult result = dispatchPassIntent(IntentOrigin::Browser, seat);
      message = result.message;
      return result.accepted();
    }

    case WebControl::PauseResume: {
      if (hubState != HubState::Running && hubState != HubState::Paused) {
        message = "Pause/resume is unavailable in this state";
        return false;
      }
      const IntentType type = hubState == HubState::Running
          ? IntentType::Pause
          : IntentType::Resume;
      const IntentResult result = dispatchSeatIntent(
          type,
          IntentOrigin::Browser,
          seat);
      message = result.message;
      return result.accepted();
    }

    case WebControl::Concede: {
      const IntentResult result = dispatchSeatIntent(
          IntentType::Concede,
          IntentOrigin::Browser,
          seat);
      message = result.message;
      return result.accepted();
    }

    case WebControl::ClaimWin: {
      const IntentResult result = dispatchSeatIntent(IntentType::ClaimWin, IntentOrigin::Browser, seat);
      message = result.message;
      return result.accepted();
    }

    case WebControl::ConfirmWin: {
      const IntentResult result = dispatchSeatIntent(IntentType::ConfirmWin, IntentOrigin::Browser, seat);
      message = result.message;
      return result.accepted();
    }

    case WebControl::DenyWin: {
      const IntentResult result = dispatchSeatIntent(IntentType::DenyWin, IntentOrigin::Browser, seat);
      message = result.message;
      return result.accepted();
    }
  }

  message = "Unknown web control";
  return false;
}

void handlePass(uint8_t sigilId) {
  if (hubState == HubState::Lobby) {
    if (lobby.isHeld(sigilId)) {
      lobby.setSharedChord(sigilId, true);
      lobby.setSuppressNextShort(sigilId, true);

      if (lobby.startArmedBy() == sigilId) {
        lobby.clearStartArm();
      }

      bool added = false;
      PlayerSeat affected;
      if (lobby.toggleSecondary(sigilId, added, affected)) {
        Serial.print("ATLAS|LOBBY|SECONDARY|");
        Serial.print(sigilId);
        Serial.print("|");
        Serial.print(added ? "ADDED" : "REMOVED");
        Serial.print("|PLAYER|");
        Serial.println(affected.playerNumber);
        if (added) {
          audio.sharedPlayerAdded(sigilId);
        } else {
          audio.sharedPlayerRemoved(sigilId);
        }
        leds.invalidateAll();
      }
      return;
    }

    if (sigilId == lobby.hostModule() && lobby.playerCount() >= 2) {
      PlayerSeat selected;
      if (lobby.randomStarter(selected)) {
        Serial.print("ATLAS|LOBBY|RANDOM_STARTER|");
        Serial.println(selected.playerNumber);
        audio.randomStarter(selected.moduleId);
      }
    }
    return;
  }

  if (hubState == HubState::Paused) {
    if (game.hasWinClaim()) {
      const uint8_t expectedNumber = game.nextWinConfirmationPlayerNumber();
      const PlayerSeat *expected = game.playerByNumber(expectedNumber);
      if (expected == nullptr || expected->moduleId != sigilId) {
        return;
      }

      dispatchSeatIntent(IntentType::DenyWin, IntentOrigin::PhysicalSigil, *expected);
      return;
    }

    if (lobby.isHeld(sigilId)) {
      eliminationChord[sigilId] = true;
      suppressEliminationShort[sigilId] = true;
      beginEliminationSelection(sigilId);
      return;
    }

    if (eliminationTargetPlayer != 0) {
      confirmElimination(sigilId);
    }
    return;
  }

  if (hubState != HubState::Running) {
    return;
  }

  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || active->moduleId != sigilId) {
    return;
  }

  dispatchPassIntent(IntentOrigin::PhysicalSigil, *active);
}

void handleActionDown(uint8_t sigilId) {
  lobby.setHeld(sigilId, true);
  lobby.setActionLong(sigilId, false);
  lobby.setSharedChord(sigilId, false);
  eliminationChord[sigilId] = false;

  if (
      hubState == HubState::Running &&
      cancelPendingPassForModule(sigilId, "ACTION")) {
    suppressActionAfterPassCancel[sigilId] = true;
    suppressActionReleasedAtMs[sigilId] = 0;
    return;
  }

  if (hubState == HubState::Starting) {
    cancelCountdown();
  }
}

void handleActionUp(uint8_t sigilId) {
  lobby.setHeld(sigilId, false);

  const bool usedLobbyChord = lobby.sharedChord(sigilId);
  const bool usedEliminationChord = eliminationChord[sigilId];
  const bool wasLong = lobby.actionLong(sigilId);

  lobby.setSharedChord(sigilId, false);
  eliminationChord[sigilId] = false;
  lobby.setActionLong(sigilId, false);

  if (suppressActionAfterPassCancel[sigilId]) {
    suppressActionReleasedAtMs[sigilId] = millis();
    return;
  }

  if (usedLobbyChord) {
    if (wasLong) {
      lobby.setSuppressNextShort(sigilId, false);
    }
    return;
  }

  if (usedEliminationChord) {
    if (wasLong) {
      suppressEliminationShort[sigilId] = false;
    }
    return;
  }

  if (
      hubState == HubState::Lobby &&
      lobby.startArmedBy() == sigilId &&
      sigilId == lobby.hostModule()) {
    beginCountdown();
  }
}

void handleActionShort(uint8_t sigilId) {
  if (suppressActionAfterPassCancel[sigilId]) {
    suppressActionAfterPassCancel[sigilId] = false;
    suppressActionReleasedAtMs[sigilId] = 0;
    return;
  }

  if (lobby.consumeSuppressNextShort(sigilId)) {
    return;
  }

  if (suppressEliminationShort[sigilId]) {
    suppressEliminationShort[sigilId] = false;
    return;
  }

  if (hubState == HubState::Paused && game.hasWinClaim()) {
    const uint8_t expectedNumber = game.nextWinConfirmationPlayerNumber();
    const PlayerSeat *expected = game.playerByNumber(expectedNumber);
    if (expected == nullptr || expected->moduleId != sigilId) {
      return;
    }

    dispatchSeatIntent(IntentType::ConfirmWin, IntentOrigin::PhysicalSigil, *expected);
    return;
  }

  if (hubState == HubState::Paused && eliminationTargetPlayer != 0) {
    cycleEliminationTarget(sigilId);
    return;
  }

  if (hubState == HubState::Lobby) {
    handleLobbyShort(sigilId);
    return;
  }

  if (hubState == HubState::GameOver && sigilId == lobby.hostModule()) {
    enterRematchLobby();
  }
}

void handleActionLong(uint8_t sigilId) {
  if (suppressActionAfterPassCancel[sigilId]) {
    return;
  }

  lobby.setActionLong(sigilId, true);

  if (
      hubState == HubState::Lobby &&
      lobby.sharedChord(sigilId)) {
    return;
  }

  if (
      hubState == HubState::Paused &&
      eliminationChord[sigilId]) {
    return;
  }

  if (hubState == HubState::Lobby) {
    if (sigilId != lobby.hostModule()) {
      Serial.println("ATLAS|LOBBY|START_ARM|DENIED_NOT_HOST");
      return;
    }

    if (lobby.playerCount() < 2) {
      Serial.println("ATLAS|LOBBY|START_ARM|DENIED_NEED_PLAYERS");
      return;
    }

    if (lobby.anyOtherHeld(sigilId)) {
      Serial.println("ATLAS|LOBBY|START_ARM|DENIED_OTHER_HELD");
      return;
    }

    lobby.setStartArmedBy(sigilId);
    audio.startArmed(sigilId);
    Serial.print("ATLAS|LOBBY|START_ARM|");
    Serial.println(sigilId);
    return;
  }

  if (hubState == HubState::Running) {
    PlayerSeat actor;
    if (!firstLivingSeatForModule(sigilId, actor)) {
      Serial.print("ATLAS|INTENT|PAUSE|REJECTED_MODULE|");
      Serial.println(sigilId);
      return;
    }
    const IntentResult result = dispatchSeatIntent(
        IntentType::Pause,
        IntentOrigin::PhysicalSigil,
        actor);
    if (!result.accepted()) {
      Serial.print("ATLAS|INTENT|PAUSE|REJECTED|");
      Serial.println(result.message);
    }
    return;
  }

  if (hubState == HubState::Paused) {
    if (game.hasWinClaim()) {
      Serial.println("ATLAS|GAME|RESUME|DENIED_WIN_CLAIM");
      return;
    }

    if (eliminationTargetPlayer != 0) {
      cancelEliminationSelection();
      return;
    }

    PlayerSeat actor;
    if (!firstLivingSeatForModule(sigilId, actor)) {
      Serial.print("ATLAS|INTENT|RESUME|REJECTED_MODULE|");
      Serial.println(sigilId);
      return;
    }
    const IntentResult result = dispatchSeatIntent(
        IntentType::Resume,
        IntentOrigin::PhysicalSigil,
        actor);
    if (!result.accepted()) {
      Serial.print("ATLAS|INTENT|RESUME|REJECTED|");
      Serial.println(result.message);
    }
    return;
  }

  if (hubState == HubState::GameOver && sigilId == lobby.hostModule()) {
    enterEmptyLobby();
  }
}

void handleActionWin(uint8_t sigilId) {
  if (suppressActionAfterPassCancel[sigilId]) {
    return;
  }

  if (
      hubState == HubState::Lobby &&
      sigilId == lobby.hostModule() &&
      lobby.startArmedBy() == sigilId) {
    enterEmptyLobby();
    return;
  }

  if (hubState != HubState::Paused || eliminationTargetPlayer != 0) {
    return;
  }

  if (game.hasWinClaim()) {
    return;
  }

  const PlayerSeat *active = game.activePlayer();
  if (
      active == nullptr ||
      sigilId != winArmedModule ||
      winArmedPlayer == 0 ||
      active->moduleId != sigilId ||
      active->playerNumber != winArmedPlayer ||
      game.isEliminated(active->playerNumber)) {
    Serial.print("ATLAS|GAME|WIN|IGNORED|SIGIL|");
    Serial.println(sigilId);
    return;
  }

  dispatchSeatIntent(IntentType::ClaimWin, IntentOrigin::PhysicalSigil, *active,
      TurnHub::CLAIM_FROM_ARMED_PAUSE);
}

void processSigilEvents() {
  SigilEvent event;
  while (sigilBus.poll(event)) {
    switch (event.type) {
      case PacketType::Hello:
        leds.invalidate(event.sigilId);
        break;
      case PacketType::Pass:
        handlePass(event.sigilId);
        break;
      case PacketType::ActionDown:
        handleActionDown(event.sigilId);
        break;
      case PacketType::ActionUp:
        handleActionUp(event.sigilId);
        break;
      case PacketType::ActionShort:
        handleActionShort(event.sigilId);
        break;
      case PacketType::ActionLong:
        handleActionLong(event.sigilId);
        break;
      case PacketType::ActionWin:
        handleActionWin(event.sigilId);
        break;
      default:
        break;
    }
  }
}

void handleRoot() {
  server.sendHeader("Location", "/portal");
  server.send(302, "text/plain", "TurnHub portal");
}

void handleStatus() {
  PlayerSeat selected;
  const uint8_t starter = lobby.selectedStarter(selected)
      ? selected.playerNumber
      : (game.hasPlayers() ? game.starterPlayerNumber() : 0);

  const uint8_t active =
      (hubState == HubState::Running || hubState == HubState::Paused)
      ? game.activePlayerNumber()
      : 0;
  const uint8_t players = game.hasPlayers() ? game.playerCount() : lobby.playerCount();
  const uint8_t host = lobby.hostModule();
  const bool otaStateAllowed =
      hubState == HubState::Lobby ||
      hubState == HubState::GameOver;
  const uint32_t nowMs = millis();
  const uint32_t passElapsed = pendingPass.active
      ? nowMs - pendingPass.requestedAtMs
      : 0;
  const uint32_t passGraceRemainingMs =
      pendingPass.active && passElapsed < PASS_GRACE_MS
      ? PASS_GRACE_MS - passElapsed
      : 0;

  char json[768];
  snprintf(
      json,
      sizeof(json),
      "{\"masterButton\":%s,\"sigils\":%u,\"players\":%u,"
      "\"state\":\"%s\",\"host\":%d,\"starter\":%u,"
      "\"active\":%u,\"winner\":%u,\"eliminationTarget\":%u,"
      "\"winConfirm\":%u,\"passPending\":%u,\"passGraceMs\":%lu,"
      "\"espNow\":%s,\"firmware\":\"%s\","
      "\"build\":\"%s %s\",\"otaStateAllowed\":%s}",
      masterButtonPressed() ? "true" : "false",
      static_cast<unsigned>(sigilBus.activeCount(nowMs)),
      static_cast<unsigned>(players),
      stateName(hubState),
      host == INVALID_ID ? -1 : static_cast<int>(host),
      static_cast<unsigned>(starter),
      static_cast<unsigned>(active),
      static_cast<unsigned>(game.winnerPlayerNumber()),
      static_cast<unsigned>(eliminationTargetPlayer),
      static_cast<unsigned>(game.nextWinConfirmationPlayerNumber()),
      static_cast<unsigned>(pendingPass.active ? pendingPass.seat.playerNumber : 0),
      static_cast<unsigned long>(passGraceRemainingMs),
      espNowReady ? "true" : "false",
      TurnHubFirmware::VERSION,
      TurnHubFirmware::BUILD_DATE,
      TurnHubFirmware::BUILD_TIME,
      otaStateAllowed ? "true" : "false");

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void updateMasterButton() {
  const bool currentState = digitalRead(AtlasConfig::MASTER_BUTTON_PIN);

  if (currentState == lastButtonState) {
    return;
  }

  if (millis() - lastDebounceMs < DEBOUNCE_MS) {
    return;
  }

  lastDebounceMs = millis();
  lastButtonState = currentState;

  Serial.println(currentState == LOW
                     ? "ATLAS|MASTER_BUTTON|DOWN"
                     : "ATLAS|MASTER_BUTTON|UP");

  if (currentState == HIGH && hubState == HubState::Running) {
    const PlayerSeat *active = game.activePlayer();
    if (active != nullptr) {
      const IntentResult result = dispatchPassIntent(
          IntentOrigin::AtlasHardware,
          *active);
      if (result.accepted()) {
        if (pendingPass.active && pendingPass.seat.sameSeat(*active)) {
          Serial.println("ATLAS|MASTER_BUTTON|PASS_PENDING");
        } else {
          Serial.println("ATLAS|MASTER_BUTTON|PASS_CANCELLED");
        }
      } else {
        Serial.print("ATLAS|MASTER_BUTTON|PASS_REJECTED|");
        Serial.println(result.message);
      }
    }
  }
}

void updatePairButton() {
  const bool currentState = digitalRead(AtlasConfig::PAIR_BUTTON_PIN);

  if (currentState == lastPairButtonState) {
    return;
  }

  if (millis() - lastPairDebounceMs < DEBOUNCE_MS) {
    return;
  }

  lastPairDebounceMs = millis();
  lastPairButtonState = currentState;

  Serial.println(currentState == LOW
                     ? "ATLAS|PAIR_BUTTON|DOWN"
                     : "ATLAS|PAIR_BUTTON|UP");
}

void startNetworking() {
  WiFi.mode(WIFI_AP_STA);

  const String wifiPassword = loadOrCreateWifiPassword();
  if (wifiPassword.length() < 8) {
    Serial.println("ATLAS|WIFI_AP|PASSWORD|ERROR");
    return;
  }

  const bool apStarted = WiFi.softAP(
      AtlasConfig::WIFI_SSID,
      wifiPassword.c_str(),
      AtlasConfig::WIFI_CHANNEL,
      false,
      8);

  if (!apStarted) {
    Serial.println("ATLAS|WIFI_AP|ERROR");
    return;
  }

  Serial.print("ATLAS|WIFI_AP|READY|");
  Serial.print(AtlasConfig::WIFI_SSID);
  Serial.print("|");
  Serial.println(WiFi.softAPIP());
  Serial.println("ATLAS|WIFI_AP|SECURITY|WPA2-PSK");
  Serial.print("ATLAS|WIFI_AP|PASSWORD|");
  Serial.println(wifiPassword);

  Serial.print("ATLAS|MAC|");
  Serial.println(WiFi.macAddress());

  espNowReady = sigilBus.begin();

  TurnHubWebApi::configure(resolveWebSeat, handleWebControl);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  ota.begin();
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  Serial.println("ATLAS|WEB|READY");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(AtlasConfig::MASTER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(AtlasConfig::PAIR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(AtlasConfig::STATUS_LED_PIN, OUTPUT);
  pinMode(AtlasConfig::PAIR_LED_PIN, OUTPUT);

  lastButtonState = digitalRead(AtlasConfig::MASTER_BUTTON_PIN);
  lastPairButtonState = digitalRead(AtlasConfig::PAIR_BUTTON_PIN);

  digitalWrite(AtlasConfig::STATUS_LED_PIN, HIGH);
  digitalWrite(AtlasConfig::PAIR_LED_PIN, HIGH);

  Serial.println();
  Serial.print("ATLAS|BOOT|");
  Serial.println(TurnHubFirmware::VERSION);
  Serial.println("ATLAS|FRONT_PANEL|LEDS|ON");

  configureIntentHandlers();
  startNetworking();

  Serial.println("ATLAS|READY");
}

void loop() {
  processSigilEvents();
  updateMasterButton();
  updatePairButton();
  server.handleClient();

  const uint32_t nowMs = millis();
  updatePendingPass(nowMs);
  updateActionCancelSuppression(nowMs);
  updateCountdown(nowMs);
  audio.update(nowMs);
  leds.render(
      hubState,
      lobby,
      game,
      countdownStartedAtMs,
      eliminationTargetPlayer,
      game.nextWinConfirmationPlayerNumber(),
      nowMs);
  ota.update(nowMs);

  delay(1);
}
