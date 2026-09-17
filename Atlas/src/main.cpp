#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "game_engine.h"
#include "led_renderer.h"
#include "lobby.h"
#include "protocol.h"
#include "sigil_bus.h"
#include "turnhub_types.h"

WebServer server(AtlasConfig::HTTP_PORT);

namespace {

using TurnHub::GameEngine;
using TurnHub::HubState;
using TurnHub::INVALID_ID;
using TurnHub::LedRenderer;
using TurnHub::Lobby;
using TurnHub::MAX_PLAYERS;
using TurnHub::PlayerSeat;
using TurnHub::SigilBus;
using TurnHub::SigilEvent;
using TurnHub::stateName;
using TurnHubProtocol::PacketType;

constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t START_COUNTDOWN_MS = 3000;
constexpr uint32_t DEFAULT_WARNING_MS = 0;

SigilBus sigilBus(AtlasConfig::WIFI_CHANNEL);
Lobby lobby;
GameEngine game;
LedRenderer leds(sigilBus);

HubState hubState = HubState::Lobby;
bool espNowReady = false;
bool lastButtonState = HIGH;
uint32_t lastDebounceMs = 0;
uint32_t countdownStartedAtMs = 0;
int8_t lastCountdownSecond = -1;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#111318">
  <title>TurnHub Atlas</title>
  <style>
    :root { color-scheme: dark; }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 16px;
      font-family: system-ui, sans-serif;
      background: #0b0d11;
      color: #f3f5f7;
    }
    main {
      width: min(560px, 100%);
      padding: 28px;
      border: 1px solid #2b3240;
      border-radius: 18px;
      background: #141820;
      box-shadow: 0 18px 60px rgba(0,0,0,.28);
    }
    h1 { margin: 0 0 4px; }
    .sub { color:#9ca6b7; margin-bottom:18px; }
    .status {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 12px;
      padding: 12px 0;
      border-bottom: 1px solid #2b3240;
    }
    .status:last-child { border-bottom: 0; }
    .value { font-weight: 800; }
    .online { color: #62d58a; }
    .pressed { color: #72a7ff; }
    .muted { color: #9ca6b7; }
  </style>
</head>
<body>
  <main>
    <h1>TurnHub Atlas</h1>
    <div class="sub">ESP32 migration build</div>
    <div class="status"><span>Atlas</span><span class="value online">Online</span></div>
    <div class="status"><span>State</span><span id="state" class="value">LOBBY</span></div>
    <div class="status"><span>Online Sigils</span><span id="sigils" class="value">0</span></div>
    <div class="status"><span>Players</span><span id="players" class="value">0</span></div>
    <div class="status"><span>Host Sigil</span><span id="host" class="value muted">None</span></div>
    <div class="status"><span>Starter</span><span id="starter" class="value muted">None</span></div>
    <div class="status"><span>Active Player</span><span id="active" class="value muted">None</span></div>
    <div class="status"><span>Master Button</span><span id="button" class="value muted">Released</span></div>
    <div class="status"><span>ESP-NOW</span><span id="espnow" class="value">Starting</span></div>
  </main>
  <script>
    function showNumber(id, value, prefix) {
      const el = document.getElementById(id);
      if (!value) {
        el.textContent = 'None';
        el.className = 'value muted';
      } else {
        el.textContent = prefix + value;
        el.className = 'value';
      }
    }
    async function refresh() {
      try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        const s = await response.json();
        document.getElementById('state').textContent = s.state;
        document.getElementById('sigils').textContent = s.sigils;
        document.getElementById('players').textContent = s.players;
        showNumber('host', s.host + 1, 'Sigil ');
        showNumber('starter', s.starter, 'Player ');
        showNumber('active', s.active, 'Player ');
        const button = document.getElementById('button');
        button.textContent = s.masterButton ? 'Pressed' : 'Released';
        button.className = s.masterButton ? 'value pressed' : 'value muted';
        document.getElementById('espnow').textContent = s.espNow ? 'Ready' : 'Error';
      } catch (_) {
        document.getElementById('espnow').textContent = 'Disconnected';
      }
    }
    refresh();
    setInterval(refresh, 250);
  </script>
</body>
</html>
)HTML";

bool masterButtonPressed() {
  return digitalRead(AtlasConfig::MASTER_BUTTON_PIN) == LOW;
}

void printPlayer(const PlayerSeat &player) {
  Serial.print("Player ");
  Serial.print(player.playerNumber);
  Serial.print(" / Sigil ");
  Serial.print(player.moduleId);
  Serial.print(player.slotName());
}

void enterEmptyLobby() {
  hubState = HubState::Lobby;
  lobby.resetEmpty();
  game.reset();
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
  leds.invalidateAll();
  Serial.println("ATLAS|LOBBY|EMPTY");
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

  hubState = HubState::Lobby;
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
  lobby.clearStartArm();
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
  leds.invalidateAll();

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
  }
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
        leds.invalidateAll();
      }
      return;
    }

    if (sigilId == lobby.hostModule() && lobby.playerCount() >= 2) {
      PlayerSeat selected;
      if (lobby.randomStarter(selected)) {
        Serial.print("ATLAS|LOBBY|RANDOM_STARTER|");
        Serial.println(selected.playerNumber);
      }
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
}

void handleActionDown(uint8_t sigilId) {
  lobby.setHeld(sigilId, true);
  lobby.setActionLong(sigilId, false);
  lobby.setSharedChord(sigilId, false);

  if (hubState == HubState::Starting) {
    cancelCountdown();
  }
}

void handleActionUp(uint8_t sigilId) {
  lobby.setHeld(sigilId, false);

  const bool usedChord = lobby.sharedChord(sigilId);
  const bool wasLong = lobby.actionLong(sigilId);

  lobby.setSharedChord(sigilId, false);
  lobby.setActionLong(sigilId, false);

  if (usedChord) {
    if (wasLong) {
      lobby.setSuppressNextShort(sigilId, false);
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

  if (hubState == HubState::Lobby) {
    handleLobbyShort(sigilId);
  }
}

void handleActionLong(uint8_t sigilId) {
  lobby.setActionLong(sigilId, true);

  if (hubState == HubState::Lobby) {
    if (lobby.sharedChord(sigilId)) {
      return;
    }

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
    Serial.print("ATLAS|LOBBY|START_ARM|");
    Serial.println(sigilId);
    return;
  }

  if (hubState == HubState::Running) {
    if (game.pause(millis())) {
      hubState = HubState::Paused;
      Serial.print("ATLAS|GAME|PAUSE|SIGIL|");
      Serial.println(sigilId);
      leds.invalidateAll();
    }
    return;
  }

  if (hubState == HubState::Paused) {
    if (game.resume(millis())) {
      hubState = HubState::Running;
      Serial.print("ATLAS|GAME|RESUME|SIGIL|");
      Serial.println(sigilId);
      leds.invalidateAll();
    }
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

  if (hubState == HubState::Paused) {
    Serial.println("ATLAS|GAME|WIN|TODO_CONFIRMATION_PORT");
  }
}

void processSigilEvents() {
  SigilEvent event;
  while (sigilBus.poll(event)) {
    switch (event.type) {
      case PacketType::Hello:
        // Re-apply outputs after every HELLO. This also restores the correct
        // LEDs if a Sigil rebooted while Atlas kept running.
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
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  PlayerSeat selected;
  const uint8_t starter = lobby.selectedStarter(selected)
      ? selected.playerNumber
      : (game.running() ? game.starterPlayerNumber() : 0);

  const uint8_t active = game.running() ? game.activePlayerNumber() : 0;
  const uint8_t players = game.running() ? game.playerCount() : lobby.playerCount();
  const uint8_t host = lobby.hostModule();

  char json[256];
  snprintf(
      json,
      sizeof(json),
      "{\"masterButton\":%s,\"sigils\":%u,\"players\":%u,"
      "\"state\":\"%s\",\"host\":%d,\"starter\":%u,"
      "\"active\":%u,\"espNow\":%s}",
      masterButtonPressed() ? "true" : "false",
      static_cast<unsigned>(sigilBus.activeCount(millis())),
      static_cast<unsigned>(players),
      stateName(hubState),
      host == INVALID_ID ? -1 : static_cast<int>(host),
      static_cast<unsigned>(starter),
      static_cast<unsigned>(active),
      espNowReady ? "true" : "false");

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

  // The Atlas button is the table-level emergency/master pass. It advances
  // whichever logical player is active without pretending a particular
  // Sigil was pressed.
  if (currentState == HIGH && hubState == HubState::Running) {
    const uint8_t activeModule = game.activeModule();
    if (activeModule != INVALID_ID &&
        game.passTurn(activeModule, DEFAULT_WARNING_MS, millis())) {
      Serial.println("ATLAS|MASTER_BUTTON|PASS");
    }
  }
}

void startNetworking() {
  WiFi.mode(WIFI_AP_STA);

  const bool apStarted = WiFi.softAP(
      AtlasConfig::WIFI_SSID,
      nullptr,
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

  Serial.print("ATLAS|MAC|");
  Serial.println(WiFi.macAddress());

  espNowReady = sigilBus.begin();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
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
  Serial.println("ATLAS|BOOT");

  startNetworking();

  Serial.println("ATLAS|READY");
}

void loop() {
  const uint32_t nowMs = millis();

  processSigilEvents();
  updateMasterButton();
  updateCountdown(nowMs);
  leds.render(hubState, lobby, game, countdownStartedAtMs, nowMs);
  server.handleClient();

  delay(1);
}
