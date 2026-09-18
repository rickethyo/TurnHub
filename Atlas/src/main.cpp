#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "audio_controller.h"
#include "config.h"
#include "firmware_version.h"
#include "game_engine.h"
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
LedRenderer leds(sigilBus);
AudioController audio(sigilBus);
OtaManager ota(server, otaAllowed);

HubState hubState = HubState::Lobby;
bool espNowReady = false;
bool lastButtonState = HIGH;
uint32_t lastDebounceMs = 0;
uint32_t countdownStartedAtMs = 0;
int8_t lastCountdownSecond = -1;

uint8_t eliminationTargetPlayer = 0;
bool eliminationChord[MAX_PHYSICAL_SIGILS] = {};
bool suppressEliminationShort[MAX_PHYSICAL_SIGILS] = {};
uint8_t winArmedModule = INVALID_ID;
uint8_t winArmedPlayer = 0;

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
  Preferences prefs;
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

void clearDecisionState() {
  eliminationTargetPlayer = 0;
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
    eliminationChord[i] = false;
    suppressEliminationShort[i] = false;
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

  const uint32_t nowMs = millis();

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
      if (hubState != HubState::Running) {
        message = "Pass is only available during a running game";
        return false;
      }
      const PlayerSeat *active = game.activePlayer();
      if (active == nullptr || !active->sameSeat(seat)) {
        message = "It is not this seat's turn";
        return false;
      }

      const PlayerSeat previous = *active;
      if (!game.passTurn(moduleId, DEFAULT_WARNING_MS, nowMs)) {
        message = "Atlas rejected the pass";
        return false;
      }
      const PlayerSeat *current = game.activePlayer();
      if (current != nullptr) {
        if (previous.moduleId == current->moduleId) {
          audio.sameModulePass(current->moduleId);
        } else {
          audio.turnPass(current->moduleId);
        }
      }
      Serial.print("ATLAS|WEB|PASS|");
      Serial.print(previous.playerNumber);
      Serial.print("->");
      Serial.println(current != nullptr ? current->playerNumber : 0);
      message = "Turn passed";
      return true;
    }

    case WebControl::PauseResume: {
      if (!game.hasPlayers() || game.isEliminated(seat.playerNumber)) {
        message = "This player is not active in the game";
        return false;
      }

      if (hubState == HubState::Running) {
        if (!game.pause(nowMs)) {
          message = "Could not pause the game";
          return false;
        }
        hubState = HubState::Paused;
        winArmedModule = INVALID_ID;
        winArmedPlayer = 0;
        audio.pause(gameAudioMask());
        leds.invalidateAll();
        Serial.print("ATLAS|WEB|PAUSE|PLAYER|");
        Serial.println(seat.playerNumber);
        message = "Game paused";
        return true;
      }

      if (hubState == HubState::Paused) {
        if (game.hasWinClaim()) {
          message = "Resolve the win claim before resuming";
          return false;
        }
        if (eliminationTargetPlayer != 0) {
          message = "Resolve the elimination before resuming";
          return false;
        }
        if (!game.resume(nowMs)) {
          message = "Could not resume the game";
          return false;
        }
        hubState = HubState::Running;
        winArmedModule = INVALID_ID;
        winArmedPlayer = 0;
        audio.resume(gameAudioMask());
        leds.invalidateAll();
        Serial.print("ATLAS|WEB|RESUME|PLAYER|");
        Serial.println(seat.playerNumber);
        message = "Game resumed";
        return true;
      }

      message = "Pause/resume is unavailable in this state";
      return false;
    }

    case WebControl::Concede: {
      if (hubState != HubState::Running && hubState != HubState::Paused) {
        message = "Concede is only available during an active game";
        return false;
      }
      if (game.hasWinClaim() || eliminationTargetPlayer != 0) {
        message = "Resolve the current table decision first";
        return false;
      }
      if (game.isEliminated(seat.playerNumber)) {
        message = "This player has already left the game";
        return false;
      }

      const bool restoreRunning = hubState == HubState::Running;
      if (restoreRunning) {
        if (!game.pause(nowMs)) {
          message = "Could not prepare the concession";
          return false;
        }
        hubState = HubState::Paused;
      }

      bool gameFinished = false;
      if (!game.eliminatePlayer(
              seat.playerNumber,
              DEFAULT_WARNING_MS,
              nowMs,
              gameFinished)) {
        if (restoreRunning && game.resume(nowMs)) {
          hubState = HubState::Running;
        }
        message = "Atlas rejected the concession";
        return false;
      }

      audio.playerEliminated(moduleId);
      leds.invalidateAll();
      Serial.print("ATLAS|WEB|CONCEDE|PLAYER|");
      Serial.println(seat.playerNumber);

      if (gameFinished) {
        finishGameState();
      } else if (restoreRunning && game.resume(nowMs)) {
        hubState = HubState::Running;
      }

      message = "Player conceded";
      return true;
    }

    case WebControl::ClaimWin: {
      if (hubState != HubState::Running && hubState != HubState::Paused) {
        message = "Win claim is unavailable right now";
        return false;
      }
      if (game.hasWinClaim() || eliminationTargetPlayer != 0) {
        message = "Another table decision is already pending";
        return false;
      }
      const PlayerSeat *active = game.activePlayer();
      if (active == nullptr || !active->sameSeat(seat) ||
          game.isEliminated(seat.playerNumber)) {
        message = "Only the active player can claim a win";
        return false;
      }

      const bool restoreRunning = hubState == HubState::Running;
      if (!game.beginWinClaim(seat.playerNumber, restoreRunning, nowMs)) {
        message = "Could not start the win claim";
        return false;
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

      Serial.print("ATLAS|WEB|WIN|CLAIMED|PLAYER|");
      Serial.println(seat.playerNumber);
      message = "Win claim sent to the table";
      return true;
    }

    case WebControl::ConfirmWin: {
      if (hubState != HubState::Paused || !game.hasWinClaim()) {
        message = "There is no win claim to confirm";
        return false;
      }
      if (game.nextWinConfirmationPlayerNumber() != seat.playerNumber) {
        message = "Another player must respond first";
        return false;
      }

      bool gameFinished = false;
      if (!game.confirmWinClaim(seat.playerNumber, nowMs, gameFinished)) {
        message = "Could not confirm the win claim";
        return false;
      }
      audio.winConfirmed(gameAudioMask());
      leds.invalidateAll();
      Serial.print("ATLAS|WEB|WIN|CONFIRMED|PLAYER|");
      Serial.println(seat.playerNumber);
      if (gameFinished) {
        finishGameState();
      }
      message = "Win claim confirmed";
      return true;
    }

    case WebControl::DenyWin: {
      if (hubState != HubState::Paused || !game.hasWinClaim()) {
        message = "There is no win claim to deny";
        return false;
      }
      if (game.nextWinConfirmationPlayerNumber() != seat.playerNumber) {
        message = "Another player must respond first";
        return false;
      }
      if (!game.denyWinClaim(seat.playerNumber, nowMs)) {
        message = "Could not deny the win claim";
        return false;
      }
      hubState = game.paused() ? HubState::Paused : HubState::Running;
      audio.winDenied(gameAudioMask());
      leds.invalidateAll();
      Serial.print("ATLAS|WEB|WIN|DENIED|PLAYER|");
      Serial.println(seat.playerNumber);
      message = "Win claim denied";
      return true;
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

      if (game.denyWinClaim(expectedNumber, millis())) {
        hubState = game.paused() ? HubState::Paused : HubState::Running;
        audio.winDenied(gameAudioMask());
        leds.invalidateAll();
        Serial.print("ATLAS|GAME|WIN|DENIED|PLAYER|");
        Serial.println(expectedNumber);
      }
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

  const PlayerSeat *before = game.activePlayer();
  PlayerSeat previous;
  if (before != nullptr) {
    previous = *before;
  }

  if (!game.passTurn(sigilId, DEFAULT_WARNING_MS, millis())) {
    return;
  }

  const PlayerSeat *current = game.activePlayer();
  Serial.print("ATLAS|GAME|PASS|");
  Serial.print(previous.playerNumber);
  Serial.print("->");
  Serial.println(current != nullptr ? current->playerNumber : 0);

  if (current != nullptr) {
    if (previous.moduleId == current->moduleId) {
      audio.sameModulePass(current->moduleId);
    } else {
      audio.turnPass(current->moduleId);
    }
  }
}

void handleActionDown(uint8_t sigilId) {
  lobby.setHeld(sigilId, true);
  lobby.setActionLong(sigilId, false);
  lobby.setSharedChord(sigilId, false);
  eliminationChord[sigilId] = false;

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

    bool gameFinished = false;
    if (game.confirmWinClaim(expectedNumber, millis(), gameFinished)) {
      audio.winConfirmed(gameAudioMask());
      leds.invalidateAll();
      Serial.print("ATLAS|GAME|WIN|CONFIRMED|PLAYER|");
      Serial.println(expectedNumber);
      if (gameFinished) {
        finishGameState();
      }
    }
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
    const PlayerSeat *active = game.activePlayer();
    if (game.pause(millis())) {
      hubState = HubState::Paused;

      if (active != nullptr && active->moduleId == sigilId) {
        winArmedModule = sigilId;
        winArmedPlayer = active->playerNumber;
      } else {
        winArmedModule = INVALID_ID;
        winArmedPlayer = 0;
      }

      Serial.print("ATLAS|GAME|PAUSE|SIGIL|");
      Serial.println(sigilId);
      audio.pause(gameAudioMask());
      leds.invalidateAll();
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

    if (game.resume(millis())) {
      hubState = HubState::Running;
      winArmedModule = INVALID_ID;
      winArmedPlayer = 0;
      Serial.print("ATLAS|GAME|RESUME|SIGIL|");
      Serial.println(sigilId);
      audio.resume(gameAudioMask());
      leds.invalidateAll();
    }
    return;
  }

  if (hubState == HubState::GameOver && sigilId == lobby.hostModule()) {
    enterEmptyLobby();
  }
}

void handleActionWin(uint8_t sigilId) {
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

  if (!game.beginWinClaim(winArmedPlayer, true, millis())) {
    return;
  }

  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  leds.invalidateAll();

  if (game.gameOver()) {
    finishGameState();
    return;
  }

  audio.winClaimed(gameAudioMask());
  Serial.print("ATLAS|GAME|WIN|CLAIMED|PLAYER|");
  Serial.println(game.winClaimPlayerNumber());
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

  char json[640];
  snprintf(
      json,
      sizeof(json),
      "{\"masterButton\":%s,\"sigils\":%u,\"players\":%u,"
      "\"state\":\"%s\",\"host\":%d,\"starter\":%u,"
      "\"active\":%u,\"winner\":%u,\"eliminationTarget\":%u,"
      "\"winConfirm\":%u,\"espNow\":%s,\"firmware\":\"%s\","
      "\"build\":\"%s %s\",\"otaStateAllowed\":%s}",
      masterButtonPressed() ? "true" : "false",
      static_cast<unsigned>(sigilBus.activeCount(millis())),
      static_cast<unsigned>(players),
      stateName(hubState),
      host == INVALID_ID ? -1 : static_cast<int>(host),
      static_cast<unsigned>(starter),
      static_cast<unsigned>(active),
      static_cast<unsigned>(game.winnerPlayerNumber()),
      static_cast<unsigned>(eliminationTargetPlayer),
      static_cast<unsigned>(game.nextWinConfirmationPlayerNumber()),
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
    const PlayerSeat *before = game.activePlayer();
    PlayerSeat previous;
    if (before != nullptr) {
      previous = *before;
    }

    const uint8_t activeModule = game.activeModule();
    if (activeModule != INVALID_ID &&
        game.passTurn(activeModule, DEFAULT_WARNING_MS, millis())) {
      Serial.println("ATLAS|MASTER_BUTTON|PASS");
      const PlayerSeat *current = game.activePlayer();
      if (current != nullptr) {
        if (previous.moduleId == current->moduleId) {
          audio.sameModulePass(current->moduleId);
        } else {
          audio.turnPass(current->moduleId);
        }
      }
    }
  }
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
  lastButtonState = digitalRead(AtlasConfig::MASTER_BUTTON_PIN);

  Serial.println();
  Serial.print("ATLAS|BOOT|");
  Serial.println(TurnHubFirmware::VERSION);

  startNetworking();

  Serial.println("ATLAS|READY");
}

void loop() {
  const uint32_t nowMs = millis();

  processSigilEvents();
  updateMasterButton();
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
  server.handleClient();
  ota.update(nowMs);

  delay(1);
}
