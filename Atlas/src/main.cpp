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
#include "controller_profiles.h"
#include "profile_store.h"
#include "game_settings_store.h"
#include "runtime_diagnostics.h"

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
using TurnHub::MAX_CONTROLLERS;
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
TurnHub::GameSettings nextGameSettings;
bool gameSettingsAvailable = true;
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
constexpr uint32_t BOOT_BLINK_INTERVAL_MS = 150;
constexpr uint32_t PAIRING_DURATION_MS = 30000;
constexpr uint32_t PAIR_BLINK_INTERVAL_MS = 250;
bool bootBlinkActive = false;
uint32_t bootBlinkStartedAtMs = 0;
bool pairingActive = false;
uint32_t pairingStartedAtMs = 0;
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
    if (game.controllerInGame(id)) {
      mask |= AudioController::maskForSigil(id);
    }
  }
  return mask;
}

void printPlayer(const PlayerSeat &player) {
  Serial.print("Player ");
  Serial.print(player.playerNumber);
  Serial.print(" / Sigil ");
  Serial.print(player.controllerId);
  Serial.print(player.slotName());
}

bool seatForModuleSlot(uint8_t controllerId, uint8_t slot, PlayerSeat &seat) {
  PlayerSeat local[2];
  const uint8_t count = game.hasPlayers()
      ? game.playersForController(controllerId, local, 2)
      : lobby.playersForController(controllerId, local, 2);

  for (uint8_t i = 0; i < count; ++i) {
    if (local[i].slot == slot) {
      seat = local[i];
      return true;
    }
  }
  return false;
}

bool firstLivingSeatForModule(uint8_t controllerId, PlayerSeat &seat) {
  PlayerSeat local[2];
  const uint8_t count = game.livingPlayersForController(controllerId, local, 2);
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
      seat->controllerId != intent.actor.controllerId ||
      seat->slot != intent.actor.slot) {
    return nullptr;
  }
  return seat;
}

bool resolveWebSeat(
    uint8_t controllerId,
    uint8_t slot,
    SeatSnapshot &snapshot) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controllerId, slot, seat)) {
    snapshot = SeatSnapshot{};
    return false;
  }

  snapshot.exists = true;
  snapshot.playerNumber = seat.playerNumber;
  snapshot.active = false;
  snapshot.eliminated = false;
  snapshot.host = controllerId == lobby.hostController();
  snapshot.lifeAvailable = game.hasPlayers();
  snapshot.life = game.lifeTotal(seat.playerNumber);

  if (game.hasPlayers()) {
    snapshot.eliminated = game.isEliminated(seat.playerNumber);
    const PlayerSeat *active = game.activePlayer();
    snapshot.active = active != nullptr && active->sameSeat(seat);
  }

  return true;
}

bool resolveProfileParticipant(const String &profileId, uint8_t &controllerId, uint8_t &slot) {
  if (profileId.length() == 0) return false;
  PlayerSeat seats[MAX_PLAYERS];
  uint8_t count = 0;
  if (game.hasPlayers()) {
    count = game.playerCount();
    for (uint8_t i = 0; i < count; ++i) seats[i] = *game.playerAt(i);
  } else {
    count = lobby.buildPlayers(seats, MAX_PLAYERS);
  }
  for (uint8_t i = 0; i < count; ++i) {
    const String owner = seats[i].profileId[0] ? String(seats[i].profileId) :
        TurnHubControllers::profileForSeat(seats[i].controllerId, seats[i].slot);
    if (owner == profileId) {
      controllerId = seats[i].controllerId;
      slot = seats[i].slot;
      return true;
    }
  }
  return false;
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
  if (!pendingPass.active || pendingPass.seat.controllerId != sigilId) {
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
      intent.actor.controllerId != active->controllerId ||
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
      (intent.payload.flags & TurnHub::ARM_WIN_ON_PAUSE) != 0 &&
      active != nullptr &&
      active->controllerId == intent.actor.controllerId) {
    winArmedModule = intent.actor.controllerId;
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

IntentResult handleTogglePauseIntent(const Intent &intent, void *) {
  if (hubState != HubState::Running && hubState != HubState::Paused) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pause/resume is unavailable in this state");
  }
  Intent resolved = intent;
  resolved.type = hubState == HubState::Running ? IntentType::Pause : IntentType::Resume;
  return intents.dispatch(resolved);
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

  audio.playerEliminated(seat->controllerId);
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
      winArmedModule != seat.controllerId || winArmedPlayer != seat.playerNumber)) {
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
  intent.actor.controllerId = seat.controllerId;
  intent.actor.slot = seat.slot;
  intent.actor.playerNumber = seat.playerNumber;
  return intents.dispatch(intent);
}

IntentResult dispatchPassIntent(IntentOrigin origin, const PlayerSeat &seat) {
  return dispatchSeatIntent(IntentType::Pass, origin, seat);
}

IntentResult handleTableIntent(const Intent &intent, void *);
IntentResult handleCommitPassIntent(const Intent &intent, void *);

IntentResult handleGameSettingsIntent(const Intent &intent, void *) {
  PlayerSeat actor;
  if (hubState != HubState::Lobby || lobby.playerCount() == 0 ||
      intent.actor.controllerId != lobby.hostController() || intent.actor.slot != 1 ||
      !seatForModuleSlot(intent.actor.controllerId, intent.actor.slot, actor) ||
      actor.playerNumber != intent.actor.playerNumber)
    return IntentResult::reject(IntentStatus::Unauthorized, "Only the table host can change game settings in the lobby");
  TurnHub::GameSettings settings;
  if (intent.payload.flags >= static_cast<uint32_t>(TurnHub::GameProfile::Count))
    return IntentResult::reject(IntentStatus::Rejected, "Unknown game profile");
  settings.profile = static_cast<TurnHub::GameProfile>(intent.payload.flags);
  settings.startingLife = intent.payload.value;
  if (!TurnHub::validGameSettings(settings))
    return IntentResult::reject(IntentStatus::Rejected, "Starting life must be between 0 and 1000000");
  if (!gameSettingsAvailable || TurnHub::saveGameSettings(settings) != TurnHubStorage::Status::Ok)
    return IntentResult::reject(IntentStatus::Rejected, "Game settings could not be saved");
  nextGameSettings = settings;
  return IntentResult::accept("Game settings saved on Atlas");
}

IntentResult handleChangeLifeIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (!seat || intent.payload.targetPlayer != seat->playerNumber)
    return IntentResult::reject(IntentStatus::Unauthorized, "You can change only your own life");
  if ((hubState != HubState::Running && hubState != HubState::Paused) || eliminationTargetPlayer ||
      !game.changeLife(seat->playerNumber, intent.payload.value))
    return IntentResult::reject(IntentStatus::InvalidState, "Life cannot be changed now or exceeds its limits");
  return IntentResult::accept("Life updated");
}

IntentResult handleCounterIntent(const Intent &intent, void *) {
  if (intent.type == IntentType::ExpireLifeChanges) {
    if (intent.actor.origin != IntentOrigin::System)
      return IntentResult::reject(IntentStatus::Unauthorized, "Only Atlas expires life requests");
    game.expireLifeChanges(millis());
    return IntentResult::accept();
  }
  const auto *seat = seatForIntentActor(intent);
  if (!seat || game.isEliminated(seat->playerNumber))
    return IntentResult::reject(IntentStatus::InvalidActor, "A living participant is required");
  if ((hubState != HubState::Running && hubState != HubState::Paused) ||
      eliminationTargetPlayer || game.hasWinClaim())
    return IntentResult::reject(IntentStatus::InvalidState, "Resolve the current table decision first");
  const auto &payload = intent.payload;
  if (intent.type == IntentType::RequestLifeChange) {
    if (!game.requestLifeChange(seat->playerNumber, payload.targetPlayer, payload.value, millis()))
      return IntentResult::reject(IntentStatus::Conflict, "Request unavailable: check the target, pending request and life limits");
    return IntentResult::accept("Life change requested; Atlas accepts it after 30 seconds unless rejected");
  }
  if (intent.type == IntentType::RespondLifeChange) {
    if (payload.flags > 1 || !game.respondLifeChange(seat->playerNumber, payload.requestId, payload.flags == 1, millis()))
      return IntentResult::reject(IntentStatus::Conflict, "Request ended, changed or could not be applied; refresh the current total");
    return IntentResult::accept(payload.flags ? "Life change accepted" : "Life change rejected");
  }
  if (intent.type == IntentType::ChangeCounter) {
    if (payload.targetPlayer != seat->playerNumber)
      return IntentResult::reject(IntentStatus::Unauthorized, "Record only your own received Commander damage");
    if (!game.changeCommanderDamage(seat->playerNumber, payload.counterSource, payload.counterSlot, payload.value))
      return IntentResult::reject(IntentStatus::Conflict, "Commander damage unavailable or a counter/life limit would be exceeded");
    return IntentResult::accept("Commander damage and life updated");
  }
  return IntentResult::reject(IntentStatus::Unsupported, "Unknown counter action");
}

bool readCounters(uint8_t controller, uint8_t slot, TurnHubWebApi::CounterSnapshot &snapshot) {
  PlayerSeat seat;
  if (!game.hasPlayers() || !seatForModuleSlot(controller, slot, seat)) return false;
  snapshot = TurnHubWebApi::CounterSnapshot{};
  snapshot.player = seat.playerNumber;
  snapshot.playerCount = game.playerCount();
  snapshot.editable = (hubState == HubState::Running || hubState == HubState::Paused) &&
      !game.isEliminated(seat.playerNumber) && !eliminationTargetPlayer && !game.hasWinClaim();
  snapshot.commanderEnabled = game.settings().profile == TurnHub::GameProfile::Commander;
  for (uint8_t i = 0; i < game.playerCount(); ++i) {
    const auto source = game.playerAt(i)->playerNumber;
    snapshot.sources[i] = source;
    for (uint8_t c = 0; c < TurnHub::COMMANDERS_PER_PLAYER; ++c)
      snapshot.damage[i][c] = game.commanderDamage(seat.playerNumber, source, c + 1);
    const auto *request = game.lifeChangeFor(source);
    if (request && (request->actor == seat.playerNumber || request->target == seat.playerNumber))
      snapshot.requests[i] = *request;
  }
  return true;
}

bool changeCounter(uint8_t controller, uint8_t slot, IntentType type,
    const TurnHub::IntentPayload &payload, String &message) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controller, slot, seat)) { message = "Join the table first"; return false; }
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::Browser;
  intent.actor.controllerId = controller;
  intent.actor.slot = slot;
  intent.actor.playerNumber = seat.playerNumber;
  intent.payload = payload;
  // Commander damage is always received by the authenticated participant.
  if (type == IntentType::ChangeCounter) intent.payload.targetPlayer = seat.playerNumber;
  const auto result = intents.dispatch(intent);
  message = result.message;
  return result.accepted();
}

bool readGameSettings(TurnHub::GameSettings &settings, bool &editable) {
  settings = game.hasPlayers() ? game.settings() : nextGameSettings;
  editable = hubState == HubState::Lobby;
  return gameSettingsAvailable;
}

bool configureGame(uint8_t controller, uint8_t slot, const TurnHub::GameSettings &settings, String &message) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controller,slot,seat)) { message="Join the table first"; return false; }
  Intent intent;
  intent.type=IntentType::ConfigureGame;
  intent.actor.origin=IntentOrigin::Browser; intent.actor.controllerId=controller;
  intent.actor.slot=slot; intent.actor.playerNumber=seat.playerNumber;
  intent.payload.flags=static_cast<uint32_t>(settings.profile); intent.payload.value=settings.startingLife;
  const auto result=intents.dispatch(intent);message=result.message;return result.accepted();
}

bool changeLife(uint8_t controller, uint8_t slot, int32_t delta, String &message) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controller,slot,seat)) { message="Join the table first"; return false; }
  Intent intent;
  intent.type=IntentType::ChangeLife;
  intent.actor.origin=IntentOrigin::Browser; intent.actor.controllerId=controller;
  intent.actor.slot=slot; intent.actor.playerNumber=seat.playerNumber;
  intent.payload.targetPlayer=seat.playerNumber;intent.payload.value=delta;
  const auto result=intents.dispatch(intent);message=result.message;return result.accepted();
}

IntentResult dispatchModuleIntent(IntentType type, uint8_t controllerId,
    uint8_t slot = 1, int32_t value = 0) {
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::PhysicalSigil;
  intent.actor.controllerId = controllerId;
  intent.actor.slot = slot;
  intent.payload.value = value;
  return intents.dispatch(intent);
}

IntentResult dispatchSystemIntent(IntentType type) {
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::System;
  return intents.dispatch(intent);
}

// Pairing is deliberately authorized by the Atlas hardware Intent.
IntentResult handlePairRequestIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Use the Atlas Pair button");
  }
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pair devices in the lobby");
  }
  if (!sigilBus.openPairing()) {
    return IntentResult::reject(IntentStatus::Rejected, "Radio unavailable");
  }
  pairingActive = true;
  pairingStartedAtMs = millis();
  Serial.println("ATLAS|PAIRING|ENTER|DURATION_MS|30000");
  return IntentResult::accept("Pairing window opened");
}


IntentResult handleModerateIntent(const Intent &intent,void *) {
  using namespace TurnHubAccounts;
  const String actor(intent.payload.moderatorId),target(intent.payload.profileId);
  Account moderator,account;
  if(intent.actor.origin!=IntentOrigin::Browser||!load(actor,moderator)||moderator.archived||!(moderator.permissions&GameMaster))return IntentResult::reject(IntentStatus::Unauthorized,"Game Master permission required");
  if(!load(target,account)||account.archived)return IntentResult::reject(IntentStatus::InvalidActor,"Account unavailable or archived");
  const int action=intent.payload.value; // 0 reset, 1 remove, 2 pass, 3 mute, 4 unmute
  if(action<0||action>4)return IntentResult::reject(IntentStatus::Unsupported,"Unknown moderation action");
  if(action==3||action==4){account.nudgeMuted=action==3;return save(target,account)?IntentResult::accept("Nudge preference saved"):IntentResult::reject(IntentStatus::Rejected,"Could not save account");}
  if((action==0&&!(moderator.permissions&ResetConnections))||(action==1&&!(moderator.permissions&RemovePlayer)))return IntentResult::reject(IntentStatus::Unauthorized,"This moderation permission is disabled");
  uint8_t controller=INVALID_ID,slot=1;PlayerSeat seat;
  const bool joined=resolveProfileParticipant(target,controller,slot)&&seatForModuleSlot(controller,slot,seat);
  if(action==2){
    if(!joined||hubState!=HubState::Running||game.activePlayerNumber()!=seat.playerNumber)return IntentResult::reject(IntentStatus::InvalidState,"Target is not the active player");
    if(game.hasWinClaim()||eliminationTargetPlayer)return IntentResult::reject(IntentStatus::Conflict,"Resolve table decisions first");
    clearPendingPass("GAME_MASTER");
    if(!game.passTurn(controller,DEFAULT_WARNING_MS,millis()))return IntentResult::reject(IntentStatus::InvalidState,"Pass rejected");
    const PlayerSeat *next=game.activePlayer();if(next)audio.turnPass(next->controllerId);
    leds.invalidateAll();return IntentResult::accept("Turn passed by Game Master");
  }
  if(!TurnHubProfiles::hasPinForProfile(target))return IntentResult::reject(IntentStatus::Conflict,"Target needs a PIN for secure reconnection; ask them to set one first");
  if(action==0&&account.reconnectRequired)return IntentResult::reject(IntentStatus::Conflict,"Connections already reset");
  if(action==1){
    if(!joined)return IntentResult::reject(IntentStatus::InvalidState,"Target is not in this game");
    if(hubState==HubState::Lobby){if(slot==1&&lobby.hasSecondary(controller))return IntentResult::reject(IntentStatus::Conflict,"Remove the secondary seat first");}
    else if((hubState!=HubState::Running&&hubState!=HubState::Paused)||game.hasWinClaim()||eliminationTargetPlayer||game.isEliminated(seat.playerNumber))return IntentResult::reject(IntentStatus::InvalidState,"Resolve table decisions first, or target already removed");
  }
  Account previous=account;
  if(action==0){if(account.connectionResets==UINT32_MAX)return IntentResult::reject(IntentStatus::Rejected,"Counter full");++account.connectionResets;}
  else{if(account.gameRemovals==UINT32_MAX)return IntentResult::reject(IntentStatus::Rejected,"Counter full");++account.gameRemovals;}
  account.reconnectRequired=true;
  if(!save(target,account))return IntentResult::reject(IntentStatus::Rejected,"Could not persist moderation; no disconnection performed");
  if(action==1){
    Intent removal;removal.actor.origin=IntentOrigin::Browser;removal.actor.controllerId=controller;removal.actor.slot=slot;removal.actor.playerNumber=seat.playerNumber;
    removal.type=hubState==HubState::Lobby?IntentType::LeaveProfile:IntentType::Concede;
    strncpy(removal.payload.profileId,target.c_str(),8);
    auto result=intents.dispatch(removal);
    if(!result.accepted()){save(target,previous);return result;}
  }
  TurnHubWebApi::revokeConnections(target);
  if(joined){lobby.setHeld(controller,false);clearPendingPass("CONNECTION_RESET");}
  return IntentResult::accept(action==0?"Connections reset; sign in again to reconnect":"Removed from game");
}
bool moderateAccount(const String &actor,const String &target,const String &action,String &message){
  Intent intent;intent.type=IntentType::Moderate;intent.actor.origin=IntentOrigin::Browser;
  if(actor.length()!=8||target.length()!=8){message="Invalid account";return false;}
  strncpy(intent.payload.moderatorId,actor.c_str(),8);strncpy(intent.payload.profileId,target.c_str(),8);
  intent.payload.value=action=="reset"?0:action=="remove"?1:action=="pass"?2:action=="mute"?3:action=="unmute"?4:-1;
  const auto result=intents.dispatch(intent);message=result.message;
  TurnHub::recordActivity(
      result.accepted() ? "moderation" : "moderation_rejected",
      String("actor=") + actor + " target=" + target + " action=" + action +
          " result=" + result.message);
  return result.accepted();
}
bool configureIntentHandlers() {
  const bool moderationBound=intents.bind(IntentType::Moderate,handleModerateIntent);

  const bool settingsBound = intents.bind(IntentType::ConfigureGame, handleGameSettingsIntent);
  const bool lifeBound = intents.bind(IntentType::ChangeLife, handleChangeLifeIntent);
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
      {IntentType::TogglePause, handleTogglePauseIntent},
      {IntentType::ClaimWin, handleClaimWinIntent},
      {IntentType::ConfirmWin, handleConfirmWinIntent},
      {IntentType::DenyWin, handleDenyWinIntent},
      {IntentType::SelectStarter, handleTableIntent},
      {IntentType::Join, handleTableIntent},
      {IntentType::JoinProfile, handleTableIntent},
      {IntentType::LeaveProfile, handleTableIntent},
      {IntentType::BindProfile, handleTableIntent},
      {IntentType::Leave, handleTableIntent},
      {IntentType::ArmStart, handleTableIntent},
      {IntentType::StartGame, handleTableIntent},
      {IntentType::CancelStart, handleTableIntent},
      {IntentType::CompleteStart, handleTableIntent},
      {IntentType::Rematch, handleTableIntent},
      {IntentType::ResetGame, handleTableIntent},
      {IntentType::BeginElimination, handleTableIntent},
      {IntentType::CycleElimination, handleTableIntent},
      {IntentType::CancelElimination, handleTableIntent},
      {IntentType::Eliminate, handleTableIntent},
      {IntentType::CancelPass, handleTableIntent},
      {IntentType::CommitPass, handleCommitPassIntent},
      {IntentType::PairRequest, handlePairRequestIntent},
      {IntentType::RequestLifeChange, handleCounterIntent},
      {IntentType::RespondLifeChange, handleCounterIntent},
      {IntentType::ExpireLifeChanges, handleCounterIntent},
      {IntentType::ChangeCounter, handleCounterIntent},
  };
  for (const auto &binding : bindings) {
    const bool bound = intents.bind(binding.type, binding.handler);
    Serial.print("ATLAS|INTENT|");
    Serial.print(TurnHub::intentName(binding.type));
    Serial.println(bound ? "|BOUND" : "|BIND_FAILED");
    remainingBound = bound && remainingBound;
  }
  return moderationBound && settingsBound && lifeBound && passBound && pauseBound && resumeBound && concedeBound && remainingBound;
}

IntentResult handleCommitPassIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::System) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Only Atlas commits deferred passes");
  }
  const uint32_t nowMs = millis();
  if (!pendingPass.active) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pending pass is not ready");
  }

  const PlayerSeat *active = game.activePlayer();
  if (
      hubState != HubState::Running ||
      active == nullptr ||
      !active->sameSeat(pendingPass.seat)) {
    clearPendingPass("STATE_CHANGE");
    return IntentResult::reject(IntentStatus::InvalidState, "Pending pass is not ready");
  }

  if (nowMs - pendingPass.requestedAtMs < PASS_GRACE_MS) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pending pass is not ready");
  }

  const PendingPassState committing = pendingPass;
  pendingPass = PendingPassState{};

  if (!game.passTurn(
          committing.seat.controllerId,
          DEFAULT_WARNING_MS,
          committing.requestedAtMs)) {
    Serial.print("ATLAS|GAME|PASS|COMMIT_REJECTED|PLAYER|");
    Serial.print(committing.seat.playerNumber);
    Serial.print("|ORIGIN|");
    Serial.println(intentOriginName(committing.origin));
    leds.invalidateAll();
    return IntentResult::reject(IntentStatus::InvalidState, "Pending pass is not ready");
  }

  const PlayerSeat *current = game.activePlayer();
  Serial.print("ATLAS|GAME|PASS|COMMIT|");
  Serial.print(committing.seat.playerNumber);
  Serial.print("->");
  Serial.print(current != nullptr ? current->playerNumber : 0);
  Serial.print("|ORIGIN|");
  Serial.println(intentOriginName(committing.origin));

  if (current != nullptr) {
    if (committing.seat.controllerId == current->controllerId) {
      audio.sameModulePass(current->controllerId);
    } else {
      audio.turnPass(current->controllerId);
    }
  }
  leds.invalidateAll();
  return IntentResult::accept("Pass committed");
}

void updatePendingPass(uint32_t nowMs) {
  if (pendingPass.active && nowMs - pendingPass.requestedAtMs >= PASS_GRACE_MS) {
    dispatchSystemIntent(IntentType::CommitPass);
  }
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
  for (uint8_t id = MAX_PHYSICAL_SIGILS; id < MAX_CONTROLLERS; ++id) {
    TurnHubControllers::releaseBrowser(id);
  }
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
  if (lobby.playerCount() < 2 || !gameSettingsAvailable) {
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

  for (uint8_t i = 0; i < count; ++i) {
    const String profile = TurnHubControllers::profileForSeat(players[i].controllerId, players[i].slot);
    strncpy(players[i].profileId, profile.c_str(), sizeof(players[i].profileId) - 1);
  }

  if (!game.start(
          players,
          count,
          starter,
          DEFAULT_WARNING_MS,
          millis(), nextGameSettings)) {
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
    dispatchSystemIntent(IntentType::CompleteStart);
  }
}

void handleLobbyShort(uint8_t sigilId) {
  const auto result = dispatchModuleIntent(
      lobby.isJoined(sigilId) ? IntentType::SelectStarter : IntentType::Join,
      sigilId, 1, static_cast<int32_t>(TurnHub::StarterSelection::CycleModule));
  TurnHub::recordActivity(
      result.accepted() ? "lobby_action" : "lobby_rejected",
      String("sigil=") + String(sigilId) + " result=" + result.message);
  if (!result.accepted()) {
    Serial.print("ATLAS|LOBBY|JOIN|REJECTED|");
    Serial.print(sigilId);
    Serial.print("|");
    Serial.println(result.message);
  }
}

void beginEliminationSelection(uint8_t sigilId) {
  if (hubState != HubState::Paused || game.hasWinClaim()) {
    return;
  }

  PlayerSeat candidates[2];
  const uint8_t count = game.livingPlayersForController(sigilId, candidates, 2);
  if (count == 0) {
    return;
  }

  eliminationTargetPlayer = candidates[0].playerNumber;
  game.cancelLifeChanges();
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  audio.eliminationArmed(sigilId);
  leds.invalidateAll();

  Serial.print("ATLAS|GAME|ELIMINATION|ARMED|PLAYER|");
  Serial.println(eliminationTargetPlayer);
}

void cycleEliminationTarget(uint8_t sigilId) {
  const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
  if (target == nullptr || target->controllerId != sigilId) {
    return;
  }

  PlayerSeat candidates[2];
  const uint8_t count = game.livingPlayersForController(sigilId, candidates, 2);
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
    audio.eliminationCancelled(target->controllerId);
  }
  eliminationTargetPlayer = 0;
  leds.invalidateAll();
  Serial.println("ATLAS|GAME|ELIMINATION|CANCEL");
}

void confirmElimination(uint8_t sigilId) {
  const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
  if (target == nullptr || target->controllerId != sigilId) {
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

IntentResult handleTableIntent(const Intent &intent, void *) {
  const uint8_t module = intent.actor.controllerId;
  const uint8_t slot = intent.actor.slot;
  if (intent.type == IntentType::JoinProfile || intent.type == IntentType::LeaveProfile ||
      intent.type == IntentType::BindProfile) {
    if (hubState != HubState::Lobby) return IntentResult::reject(IntentStatus::InvalidState, "Participation changes require the lobby");
    const String profile(intent.payload.profileId);
    if (!TurnHubProfiles::profileExists(profile)) return IntentResult::reject(IntentStatus::InvalidActor, "Unknown profile");
    uint8_t existing = INVALID_ID, existingSlot = 1;
    const bool joined = resolveProfileParticipant(profile, existing, existingSlot);
    if (intent.type == IntentType::JoinProfile) {
      if (intent.actor.origin != IntentOrigin::Browser) return IntentResult::reject(IntentStatus::Unauthorized, "Profile login required");
      if (joined) return IntentResult::accept("Already at the table; this browser controls your existing player");
      const uint8_t controller = TurnHubControllers::registerBrowser(profile);
      if (controller == INVALID_ID || lobby.join(controller) == 0) {
        TurnHubControllers::releaseBrowser(controller);
        return IntentResult::reject(IntentStatus::Conflict, "Table is full");
      }
    } else if (intent.type == IntentType::LeaveProfile) {
      if (intent.actor.origin != IntentOrigin::Browser || !joined) return IntentResult::reject(IntentStatus::InvalidActor, "No participant to leave");
      if (existingSlot == 2) {
        bool added; PlayerSeat affected;
        lobby.toggleSecondary(existing, added, affected);
      } else {
        if (lobby.hasSecondary(existing)) return IntentResult::reject(IntentStatus::Conflict, "Secondary player must leave first");
        lobby.leave(existing);
      }
      TurnHubControllers::releaseBrowser(existing);
    } else {
      if (intent.actor.origin != IntentOrigin::PhysicalSigil || module >= MAX_PHYSICAL_SIGILS || (slot != 1 && slot != 2)) {
        return IntentResult::reject(IntentStatus::Unauthorized, "Physical seat confirmation required");
      }
      if (joined && existing == module && existingSlot == slot) return IntentResult::accept("Controller already attached");
      if (slot == 2 && !lobby.hasSecondary(module))
        return IntentResult::reject(IntentStatus::Conflict, "Join seat B on the Sigil first");
      const bool occupied = slot == 1 ? lobby.isJoined(module) : lobby.hasSecondary(module);
      // Physical confirmation can adopt a guest, but not another account.
      const String targetProfile = TurnHubControllers::profileForSeat(module, slot);
      if (targetProfile.length() && targetProfile != profile)
        return IntentResult::reject(IntentStatus::Conflict, "This Sigil belongs to another profile; that player must leave first");
      if (joined && existing < MAX_PHYSICAL_SIGILS && existing != module) return IntentResult::reject(IntentStatus::Conflict, "Profile already has a physical Sigil");
      if (!joined && !occupied && lobby.playerCount() >= MAX_PLAYERS) return IntentResult::reject(IntentStatus::Conflict, "Table is full");
      if (!TurnHubControllers::bindPhysical(module, slot, profile)) return IntentResult::reject(IntentStatus::Rejected, "Could not save controller assignment");
      if (joined && existing != module) {
        if (occupied) {
          // Keep the guest's table position, host and secondary seat.
          lobby.leave(existing);
        } else {
          lobby.replaceController(existing, module);
        }
        TurnHubControllers::releaseBrowser(existing);
      } else if (!joined && !occupied) {
        lobby.join(module);
      }
    }
    lobby.clearStartArm();
    leds.invalidateAll();
    return IntentResult::accept(intent.type == IntentType::LeaveProfile ? "Left the table" : "Ready at the table");
  }
  if (intent.type == IntentType::CompleteStart) {
    if (intent.actor.origin != IntentOrigin::System || hubState != HubState::Starting ||
        millis() - countdownStartedAtMs < START_COUNTDOWN_MS) {
      return IntentResult::reject(IntentStatus::InvalidState, "Countdown is not complete");
    }
    startGame();
    return hubState == HubState::Running ? IntentResult::accept("Game started") :
        IntentResult::reject(IntentStatus::InvalidState, "Could not start game");
  }
  if (module >= MAX_CONTROLLERS || (slot != 1 && slot != 2) ||
      (module >= MAX_PHYSICAL_SIGILS && (slot != 1 || intent.actor.origin == IntentOrigin::PhysicalSigil))) {
    return IntentResult::reject(IntentStatus::InvalidActor, "Invalid module or seat");
  }
  // Browser actors must still identify the current seat after lobby renumbering.
  if (intent.actor.playerNumber != 0) {
    PlayerSeat seat;
    if (!seatForModuleSlot(module, slot, seat) || seat.playerNumber != intent.actor.playerNumber) {
      return IntentResult::reject(IntentStatus::InvalidActor, "This seat is no longer at the table");
    }
  }
  switch (intent.type) {
    case IntentType::Join:
    case IntentType::Leave: {
      if (hubState != HubState::Lobby) {
        return IntentResult::reject(IntentStatus::InvalidState, "Seats can only change in the lobby");
      }
      const bool joining = intent.type == IntentType::Join;
      if (joining && lobby.playerNumber(module, slot) == 0) {
        const String profile = TurnHubControllers::profileForSeat(module, slot);
        if (module < MAX_PHYSICAL_SIGILS && !TurnHubWebApi::physicalUseAllowed(profile)) {
          return IntentResult::reject(IntentStatus::Unauthorized,
              "Sign into this profile in the portal before using its Sigil");
        }
        uint8_t current = INVALID_ID, currentSlot = 1;
        if (resolveProfileParticipant(profile, current, currentSlot)) {
          return IntentResult::reject(IntentStatus::Conflict, "Profile is already playing; attach this Sigil from its signed-in phone");
        }
      }
      if (slot == 2) {
        if (lobby.startArmedBy() == module) lobby.clearStartArm();
        if (!lobby.isJoined(module) || lobby.hasSecondary(module) == joining) {
          return IntentResult::reject(IntentStatus::Conflict, "Secondary seat is already in the requested state");
        }
        bool added = false;
        PlayerSeat affected;
        if (!lobby.toggleSecondary(module, added, affected)) {
          return IntentResult::reject(IntentStatus::InvalidState, "Could not change secondary seat");
        }
        if (added) audio.sharedPlayerAdded(module);
        else audio.sharedPlayerRemoved(module);
        Serial.print("ATLAS|LOBBY|SECONDARY|");
        Serial.print(module);
        Serial.print(added ? "|ADDED|PLAYER|" : "|REMOVED|PLAYER|");
        Serial.println(affected.playerNumber);
      } else if (joining) {
        if (lobby.isJoined(module)) return IntentResult::accept("Already joined");
        const uint8_t player = lobby.join(module);
        if (player == 0) return IntentResult::reject(IntentStatus::InvalidState, "Could not join");
        Serial.print("ATLAS|LOBBY|JOIN|SIGIL|");
        Serial.print(module);
        Serial.print("|PLAYER|");
        Serial.println(player);
        if (module == lobby.hostController()) {
          Serial.print("ATLAS|LOBBY|HOST|");
          Serial.println(module);
        }
        audio.playerJoined(module);
      } else {
        if (!lobby.leave(module)) return IntentResult::reject(IntentStatus::InvalidActor, "Module is not joined");
        Serial.print("ATLAS|LOBBY|LEAVE|SIGIL|");
        Serial.println(module);
      }
      leds.invalidateAll();
      return IntentResult::accept(joining ? "Joined" : "Left");
    }
    case IntentType::SelectStarter: {
      if (hubState != HubState::Lobby) {
        return IntentResult::reject(IntentStatus::InvalidState, "Starter can only be selected in the lobby");
      }
      PlayerSeat selected;
      bool selectedOk = false;
      const auto selection = static_cast<TurnHub::StarterSelection>(intent.payload.value);
      if (selection == TurnHub::StarterSelection::Random) {
        if (module != lobby.hostController() || lobby.playerCount() < 2) {
          return IntentResult::reject(IntentStatus::Unauthorized, "Only the host can choose a random starter with two players");
        }
        selectedOk = lobby.randomStarter(selected);
      } else if (selection == TurnHub::StarterSelection::CycleModule) {
        selectedOk = lobby.selectStarter(module, selected);
      } else if (selection == TurnHub::StarterSelection::ExactSeat) {
        selectedOk = lobby.selectStarterSeat(module, slot, selected);
      }
      if (!selectedOk) return IntentResult::reject(IntentStatus::InvalidActor, "Could not select this seat as starter");
      if (selection == TurnHub::StarterSelection::Random) audio.randomStarter(selected.controllerId);
      else audio.starterSelected(selected.controllerId);
      leds.invalidateAll();
      Serial.print("ATLAS|INTENT|SELECT_STARTER|PLAYER|");
      Serial.println(selected.playerNumber);
      return IntentResult::accept("Selected as starting player");
    }
    case IntentType::ArmStart:
    case IntentType::StartGame:
      if (!gameSettingsAvailable) return IntentResult::reject(IntentStatus::InvalidState, "Game settings storage is unavailable; restart Atlas after resolving the storage problem");
      if (hubState != HubState::Lobby || module != lobby.hostController() || lobby.playerCount() < 2) {
        return IntentResult::reject(IntentStatus::InvalidState, "Only the host can start a lobby with two players");
      }
      if (intent.type == IntentType::ArmStart) {
        if (lobby.anyOtherHeld(module)) return IntentResult::reject(IntentStatus::Conflict, "Another controller is held");
        lobby.setStartArmedBy(module);
        audio.startArmed(module);
        Serial.print("ATLAS|LOBBY|START_ARM|");
        Serial.println(module);
      } else {
        if (intent.actor.origin != IntentOrigin::Browser && lobby.startArmedBy() != module) return IntentResult::reject(IntentStatus::InvalidState, "Start is not armed");
        beginCountdown();
      }
      return IntentResult::accept();
    case IntentType::CancelStart:
      if (hubState != HubState::Starting) return IntentResult::reject(IntentStatus::InvalidState, "No countdown");
      // Existing behavior: any discovered module can cancel the countdown.
      cancelCountdown();
      return IntentResult::accept();
    case IntentType::Rematch:
    case IntentType::ResetGame:
      if (module != lobby.hostController()) return IntentResult::reject(IntentStatus::Unauthorized, "Only the host can reset");
      if (intent.type == IntentType::Rematch) {
        if (hubState != HubState::GameOver) return IntentResult::reject(IntentStatus::InvalidState, "Game is not over");
        enterRematchLobby();
      } else {
        if (hubState != HubState::GameOver &&
            !(hubState == HubState::Lobby && (lobby.startArmedBy() == module || intent.actor.origin == IntentOrigin::Browser))) {
          return IntentResult::reject(IntentStatus::InvalidState, "Reset is not available");
        }
        enterEmptyLobby();
      }
      return IntentResult::accept();
    case IntentType::CancelPass:
      if (hubState != HubState::Running || !cancelPendingPassForModule(module, "ACTION")) {
        return IntentResult::reject(IntentStatus::InvalidState, "No pending pass for this controller");
      }
      return IntentResult::accept("Pass cancelled");
    case IntentType::BeginElimination:
    case IntentType::CycleElimination:
    case IntentType::CancelElimination:
    case IntentType::Eliminate: {
      if (hubState != HubState::Paused || game.hasWinClaim()) {
        return IntentResult::reject(IntentStatus::InvalidState, "Elimination requires a pause without a win claim");
      }
      if (intent.type == IntentType::BeginElimination) {
        PlayerSeat actor;
        if (!firstLivingSeatForModule(module, actor)) return IntentResult::reject(IntentStatus::InvalidActor, "No living seat");
        beginEliminationSelection(module);
      } else {
        const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
        if (target == nullptr) return IntentResult::reject(IntentStatus::InvalidState, "No elimination is selected");
        if (intent.type == IntentType::CancelElimination) {
          // Preserve the existing table-wide cancellation gesture.
          cancelEliminationSelection();
        } else {
          if (target->controllerId != module) return IntentResult::reject(IntentStatus::Unauthorized, "Another controller owns this selection");
          if (intent.type == IntentType::CycleElimination) cycleEliminationTarget(module);
          else confirmElimination(module);
        }
      }
      return IntentResult::accept();
    }
    default:
      return IntentResult::reject(IntentStatus::Unsupported, "Unknown table intent");
  }
}

bool handleWebControl(
    uint8_t controllerId,
    uint8_t slot,
    WebControl control,
    String &message) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controllerId, slot, seat)) {
    message = "This seat is no longer at the table";
    return false;
  }

  switch (control) {
    case WebControl::Start:
    case WebControl::CancelStart:
    case WebControl::Rematch:
    case WebControl::Reset: {
      const IntentType type = control == WebControl::Start ? IntentType::StartGame :
          control == WebControl::CancelStart ? IntentType::CancelStart :
          control == WebControl::Rematch ? IntentType::Rematch : IntentType::ResetGame;
      const auto result = dispatchSeatIntent(type, IntentOrigin::Browser, seat);
      message = result.message;
      return result.accepted();
    }
    case WebControl::Join:
    case WebControl::Leave:
    case WebControl::AttachPhysical:
      message = "Use authenticated profile participation";
      return false;
    case WebControl::SelectStarter: {
      const IntentResult result = dispatchSeatIntent(IntentType::SelectStarter, IntentOrigin::Browser, seat);
      message = result.message;
      return result.accepted();
    }


    case WebControl::Pass: {
      const IntentResult result = dispatchPassIntent(IntentOrigin::Browser, seat);
      message = result.message;
      return result.accepted();
    }

    case WebControl::PauseResume: {
      const IntentResult result = dispatchSeatIntent(IntentType::TogglePause, IntentOrigin::Browser, seat);
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

bool handleProfileControl(const String &profileId, WebControl control,
    uint8_t controllerId, uint8_t slot, String &message) {
  Intent intent;
  intent.type = control == WebControl::Join ? IntentType::JoinProfile :
      control == WebControl::Leave ? IntentType::LeaveProfile : IntentType::BindProfile;
  intent.actor.origin = control == WebControl::AttachPhysical ? IntentOrigin::PhysicalSigil : IntentOrigin::Browser;
  intent.actor.controllerId = controllerId;
  intent.actor.slot = slot;
  strncpy(intent.payload.profileId, profileId.c_str(), sizeof(intent.payload.profileId) - 1);
  const auto result = intents.dispatch(intent);
  message = result.message;
  return result.accepted();
}

void handlePass(uint8_t sigilId) {
  if (hubState == HubState::Lobby) {
    if (lobby.isHeld(sigilId)) {
      lobby.setSharedChord(sigilId, true);
      lobby.setSuppressNextShort(sigilId, true);

      const auto result = dispatchModuleIntent(
          lobby.hasSecondary(sigilId) ? IntentType::Leave : IntentType::Join,
          sigilId, 2);
      if (!result.accepted()) {
        Serial.print("ATLAS|LOBBY|SECONDARY|REJECTED|");
        Serial.print(sigilId);
        Serial.print("|");
        Serial.println(result.message);
      }
      return;
    }

    const auto result = dispatchModuleIntent(IntentType::SelectStarter, sigilId, 1,
        static_cast<int32_t>(TurnHub::StarterSelection::Random));
    if (!result.accepted()) {
      Serial.print("ATLAS|LOBBY|STARTER|REJECTED|");
      Serial.print(sigilId);
      Serial.print("|");
      Serial.println(result.message);
    }
    return;
  }

  if (hubState == HubState::Paused) {
    if (game.hasWinClaim()) {
      const uint8_t expectedNumber = game.nextWinConfirmationPlayerNumber();
      const PlayerSeat *expected = game.playerByNumber(expectedNumber);
      if (expected == nullptr || expected->controllerId != sigilId) {
        return;
      }

      dispatchSeatIntent(IntentType::DenyWin, IntentOrigin::PhysicalSigil, *expected);
      return;
    }

    if (lobby.isHeld(sigilId)) {
      eliminationChord[sigilId] = true;
      suppressEliminationShort[sigilId] = true;
      dispatchModuleIntent(IntentType::BeginElimination, sigilId);
      return;
    }

    if (eliminationTargetPlayer != 0) {
      dispatchModuleIntent(IntentType::Eliminate, sigilId);
    }
    return;
  }

  if (hubState != HubState::Running) {
    return;
  }

  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || active->controllerId != sigilId) {
    return;
  }

  const auto result = dispatchPassIntent(IntentOrigin::PhysicalSigil, *active);
  if (!result.accepted()) {
    Serial.print("ATLAS|GAME|PASS|REJECTED|");
    Serial.print(sigilId);
    Serial.print("|");
    Serial.println(result.message);
  }
}

void handleActionDown(uint8_t sigilId) {
  lobby.setHeld(sigilId, true);
  lobby.setActionLong(sigilId, false);
  lobby.setSharedChord(sigilId, false);
  eliminationChord[sigilId] = false;

  if (
      hubState == HubState::Running &&
      dispatchModuleIntent(IntentType::CancelPass, sigilId).accepted()) {
    suppressActionAfterPassCancel[sigilId] = true;
    suppressActionReleasedAtMs[sigilId] = 0;
    return;
  }

  if (hubState == HubState::Starting) {
    dispatchModuleIntent(IntentType::CancelStart, sigilId);
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
      sigilId == lobby.hostController()) {
    dispatchModuleIntent(IntentType::StartGame, sigilId);
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
    if (expected == nullptr || expected->controllerId != sigilId) {
      return;
    }

    dispatchSeatIntent(IntentType::ConfirmWin, IntentOrigin::PhysicalSigil, *expected);
    return;
  }

  if (hubState == HubState::Paused && eliminationTargetPlayer != 0) {
    dispatchModuleIntent(IntentType::CycleElimination, sigilId);
    return;
  }

  if (hubState == HubState::Lobby) {
    handleLobbyShort(sigilId);
    return;
  }

  if (hubState == HubState::GameOver && sigilId == lobby.hostController()) {
    dispatchModuleIntent(IntentType::Rematch, sigilId);
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
    dispatchModuleIntent(IntentType::ArmStart, sigilId);
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
        actor, TurnHub::ARM_WIN_ON_PAUSE);
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
      dispatchModuleIntent(IntentType::CancelElimination, sigilId);
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

  if (hubState == HubState::GameOver && sigilId == lobby.hostController()) {
    dispatchModuleIntent(IntentType::ResetGame, sigilId);
  }
}

void handleActionWin(uint8_t sigilId) {
  if (suppressActionAfterPassCancel[sigilId]) {
    return;
  }

  if (
      hubState == HubState::Lobby &&
      sigilId == lobby.hostController() &&
      lobby.startArmedBy() == sigilId) {
    dispatchModuleIntent(IntentType::ResetGame, sigilId);
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
      active->controllerId != sigilId ||
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
  if (hubState != HubState::Lobby) sigilBus.closePairing();
  SigilEvent event;
  while (sigilBus.poll(event)) {
    if (event.sigilId >= MAX_PHYSICAL_SIGILS) continue;
    if (event.type != PacketType::Hello) {
      const char *kind = "sigil_event";
      switch (event.type) {
        case PacketType::Pass: kind = "sigil_pass"; break;
        case PacketType::ActionDown: kind = "sigil_down"; break;
        case PacketType::ActionUp: kind = "sigil_up"; break;
        case PacketType::ActionShort: kind = "sigil_short"; break;
        case PacketType::ActionLong: kind = "sigil_long"; break;
        case PacketType::ActionWin: kind = "sigil_win"; break;
        default: break;
      }
      TurnHub::recordActivity(kind, String("sigil=") + String(event.sigilId));
    }
    // Keep forced connection resets effective for an occupied Sigil, but let an
    // unjoined Sigil reach the normal lobby join validator. That validator can
    // return the policy/PIN reason instead of losing the button event silently.
    if (event.type != PacketType::Hello &&
        (hubState != HubState::Lobby || lobby.isJoined(event.sigilId))) {
      bool blocked = false;
      for (uint8_t slot = 1; slot <= 2; ++slot) {
        // A saved secondary binding is not an active connection until that
        // seat joins. Do not let an archived or reset placeholder disable the
        // primary seat on the same physical Sigil.
        if (lobby.playerNumber(event.sigilId, slot) == 0) continue;
        const String id = TurnHubControllers::existingProfileForSeat(event.sigilId, slot);
        if (id.length() && TurnHubWebApi::connectionBlocked(id)) {
          Serial.print("ATLAS|SIGIL|CONNECTION_BLOCKED|");
          Serial.print(event.sigilId);
          Serial.print("|SLOT|");
          Serial.print(slot);
          Serial.print("|PROFILE|");
          Serial.println(id);
          TurnHub::recordActivity(
              "connection_blocked",
              String("sigil=") + String(event.sigilId) + " slot=" + String(slot) +
                  " profile=" + id);
          blocked = true;
        }
      }
      if (blocked) continue;
    }
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
  const uint8_t host = lobby.hostController();
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
  if (currentState == LOW) {
    Intent intent;
    intent.type = IntentType::PairRequest;
    intent.actor.origin = IntentOrigin::AtlasHardware;
    intents.dispatch(intent);
  }
}

void updateFrontPanelLeds(uint32_t nowMs) {
  const uint32_t bootElapsed = nowMs - bootBlinkStartedAtMs;
  if (bootBlinkActive && bootElapsed >= 6 * BOOT_BLINK_INTERVAL_MS) {
    bootBlinkActive = false;
  }
  // Three status flashes, then steady on. Pair LED is reserved for pairing.
  digitalWrite(AtlasConfig::STATUS_LED_PIN,
      !bootBlinkActive || (bootElapsed / BOOT_BLINK_INTERVAL_MS) % 2 == 0 ? HIGH : LOW);

  const uint32_t pairingElapsed = nowMs - pairingStartedAtMs;
  if (pairingActive && (pairingElapsed >= PAIRING_DURATION_MS || hubState != HubState::Lobby)) {
    pairingActive = false;
    Serial.println("ATLAS|PAIRING|EXIT");
  }
  digitalWrite(AtlasConfig::PAIR_LED_PIN,
      pairingActive && (pairingElapsed / PAIR_BLINK_INTERVAL_MS) % 2 == 0 ? HIGH : LOW);
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

  TurnHubWebApi::configure(resolveWebSeat, handleWebControl, handleProfileControl, resolveProfileParticipant);
  TurnHubWebApi::configureGameControls(readGameSettings, configureGame, changeLife);
  TurnHubWebApi::configureCounterControls(readCounters, changeCounter);
  TurnHubWebApi::configureModeration(moderateAccount);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/diagnostics", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    if(TurnHubWebApi::requirePermission(server,TurnHubAccounts::Developer))server.send(200,"application/json",TurnHub::runtimeDiagnosticsJson());
  });
  server.on("/api/diagnostics/activity", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    if(TurnHubWebApi::requirePermission(server,TurnHubAccounts::Developer))server.send(200,"application/json",TurnHub::activityJson());
  });
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
  digitalWrite(AtlasConfig::PAIR_LED_PIN, LOW);

  Serial.println();
  Serial.print("ATLAS|BOOT|");
  Serial.println(TurnHubFirmware::VERSION);
  Serial.print("ATLAS|RESET_REASON|"); Serial.println(TurnHub::resetReason());
  Serial.print("ATLAS|DIAGNOSTICS|"); Serial.println(TurnHub::runtimeDiagnosticsJson());
  TurnHub::recordActivity("boot", TurnHub::resetReason());
  Serial.println("ATLAS|FRONT_PANEL|LEDS|BOOT_BLINK");

  configureIntentHandlers();
  const auto settingsStatus = TurnHub::loadGameSettings(nextGameSettings);
  gameSettingsAvailable = settingsStatus == TurnHubStorage::Status::Ok ||
      settingsStatus == TurnHubStorage::Status::NotFound;
  if (!gameSettingsAvailable) Serial.println("ATLAS|GAME_SETTINGS|STORAGE_ERROR");
  startNetworking();

  // Start after synchronous network setup so all three flashes are visible.
  bootBlinkStartedAtMs = millis();
  bootBlinkActive = true;
  Serial.println("ATLAS|READY");
}

void loop() {
  processSigilEvents();
  updateMasterButton();
  updatePairButton();
  server.handleClient();

  const uint32_t nowMs = millis();
  updateFrontPanelLeds(nowMs);
  updatePendingPass(nowMs);
  updateActionCancelSuppression(nowMs);
  updateCountdown(nowMs);
  dispatchSystemIntent(IntentType::ExpireLifeChanges);
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
