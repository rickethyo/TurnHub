#include "led_renderer.h"

#include <math.h>

namespace TurnHub {

namespace {
constexpr uint32_t LOBBY_IDLE_LED_MS = 500;
constexpr uint32_t LOBBY_FLASH_ON_MS = 180;
constexpr uint32_t LOBBY_FLASH_OFF_MS = 180;
constexpr uint32_t LOBBY_FLASH_GAP_MS = 3000;
constexpr uint32_t START_COUNTDOWN_FLASH_MS = 250;
constexpr uint32_t NEW_TURN_FLASH_MS = 3000;
constexpr uint32_t NEW_TURN_FLASH_INTERVAL_MS = 200;
constexpr uint32_t BREATHE_PERIOD_MS = 2600;
constexpr uint32_t WARNING_BLINK_INTERVAL_MS = 500;
}

LedRenderer::LedRenderer(SigilBus &bus)
    : bus_(bus) {
  invalidateAll();
}

void LedRenderer::invalidate(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) {
    cache_[sigilId].valid = false;
  }
}

void LedRenderer::invalidateAll() {
  for (auto &entry : cache_) {
    entry.valid = false;
  }
}

void LedRenderer::set(
    uint8_t sigilId,
    uint8_t blue,
    bool red,
    bool green) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) {
    return;
  }

  Cache &cache = cache_[sigilId];

  if (!cache.valid || cache.blue != blue) {
    bus_.setBlue(sigilId, blue);
    cache.blue = blue;
  }
  if (!cache.valid || cache.red != red) {
    bus_.setRed(sigilId, red);
    cache.red = red;
  }
  if (!cache.valid || cache.green != green) {
    bus_.setGreen(sigilId, green);
    cache.green = green;
  }

  cache.valid = true;
}

void LedRenderer::off(uint8_t sigilId) {
  set(sigilId, 0, false, false);
}

uint8_t LedRenderer::breatheValue(uint32_t nowMs) {
  const float phase = static_cast<float>(nowMs % BREATHE_PERIOD_MS)
      / static_cast<float>(BREATHE_PERIOD_MS);
  const float sine = (sinf(phase * 2.0f * PI - PI / 2.0f) + 1.0f) / 2.0f;
  const float shaped = powf(sine, 2.2f);
  return static_cast<uint8_t>(shaped * 255.0f);
}

bool LedRenderer::playerNumberRedOn(
    uint8_t playerNumber,
    uint32_t nowMs) {
  if (playerNumber == 0) {
    return false;
  }

  const uint32_t flashBlock = LOBBY_FLASH_ON_MS + LOBBY_FLASH_OFF_MS;
  const uint32_t sequenceLength =
      static_cast<uint32_t>(playerNumber) * flashBlock + LOBBY_FLASH_GAP_MS;
  const uint32_t position = nowMs % sequenceLength;

  for (uint8_t i = 0; i < playerNumber; ++i) {
    const uint32_t start = static_cast<uint32_t>(i) * flashBlock;
    if (position >= start && position < start + LOBBY_FLASH_ON_MS) {
      return true;
    }
  }
  return false;
}

bool LedRenderer::seatPulse(uint8_t slot, bool shared, uint32_t nowMs) {
  if (!shared) {
    return true;
  }

  constexpr uint32_t pulseOn = 180;
  constexpr uint32_t pulseOff = 180;
  const uint32_t position = nowMs % 1800;

  if (slot == 1) {
    return position < pulseOn;
  }

  const uint32_t secondStart = pulseOn + pulseOff;
  return position < pulseOn ||
      (position >= secondStart && position < secondStart + pulseOn);
}

uint8_t LedRenderer::starterBlueValue(
    const Lobby &lobby,
    uint8_t sigilId,
    uint32_t nowMs) {
  PlayerSeat starter;
  if (!lobby.selectedStarter(starter) || starter.moduleId != sigilId) {
    return 0;
  }

  return seatPulse(starter.slot, lobby.hasSecondary(sigilId), nowMs)
      ? 255
      : 0;
}

void LedRenderer::renderUnjoined(uint8_t sigilId, uint32_t nowMs) {
  const uint32_t cycle = LOBBY_IDLE_LED_MS * 3;
  const uint32_t position = nowMs % cycle;

  if (position < LOBBY_IDLE_LED_MS) {
    set(sigilId, 255, false, false);
  } else if (position < LOBBY_IDLE_LED_MS * 2) {
    set(sigilId, 0, false, true);
  } else {
    set(sigilId, 0, true, false);
  }
}

void LedRenderer::renderLobby(
    uint8_t sigilId,
    const Lobby &lobby,
    uint32_t nowMs) {
  if (!lobby.isJoined(sigilId)) {
    renderUnjoined(sigilId, nowMs);
    return;
  }

  const uint8_t player = lobby.playerNumber(sigilId, 1);
  set(
      sigilId,
      starterBlueValue(lobby, sigilId, nowMs),
      playerNumberRedOn(player, nowMs),
      sigilId == lobby.hostModule());
}

void LedRenderer::renderStarting(
    uint8_t sigilId,
    const Lobby &lobby,
    uint32_t countdownStartedAtMs,
    uint32_t nowMs) {
  if (!lobby.isJoined(sigilId)) {
    renderUnjoined(sigilId, nowMs);
    return;
  }

  const uint32_t elapsed = nowMs - countdownStartedAtMs;
  const uint32_t withinSecond = elapsed % 1000;
  set(
      sigilId,
      withinSecond < START_COUNTDOWN_FLASH_MS ? 255 : 0,
      false,
      false);
}

void LedRenderer::renderRunning(
    uint8_t sigilId,
    const GameEngine &game,
    uint32_t nowMs) {
  if (!game.moduleInGame(sigilId)) {
    off(sigilId);
    return;
  }

  if (sigilId != game.activeModule()) {
    set(sigilId, 255, false, false);
    return;
  }

  const uint32_t turnElapsed = game.currentTurnElapsedMs(nowMs);
  uint8_t blue = 0;

  if (turnElapsed < NEW_TURN_FLASH_MS) {
    blue = ((turnElapsed / NEW_TURN_FLASH_INTERVAL_MS) % 2 == 0) ? 255 : 0;
  } else {
    blue = breatheValue(nowMs);
  }

  bool red = false;
  bool green = false;

  switch (game.warningPhase(nowMs)) {
    case WarningPhase::Normal:
      break;
    case WarningPhase::Caution:
    case WarningPhase::OffGreen:
      green = true;
      break;
    case WarningPhase::Warning:
      red = ((nowMs / WARNING_BLINK_INTERVAL_MS) % 2) == 0;
      break;
  }

  set(sigilId, blue, red, green);
}

void LedRenderer::renderPaused(
    uint8_t sigilId,
    const GameEngine &game,
    uint8_t eliminationTargetPlayer,
    uint8_t winConfirmationPlayer,
    uint32_t nowMs) {
  if (!game.moduleInGame(sigilId)) {
    off(sigilId);
    return;
  }

  const PlayerSeat *winTarget = game.playerByNumber(winConfirmationPlayer);
  if (winTarget != nullptr && winTarget->moduleId == sigilId) {
    PlayerSeat living[2];
    const uint8_t count = game.livingPlayersForModule(sigilId, living, 2);
    set(
        sigilId,
        0,
        false,
        seatPulse(winTarget->slot, count > 1, nowMs));
    return;
  }

  const PlayerSeat *eliminationTarget = game.playerByNumber(eliminationTargetPlayer);
  if (eliminationTarget != nullptr && eliminationTarget->moduleId == sigilId) {
    PlayerSeat living[2];
    const uint8_t count = game.livingPlayersForModule(sigilId, living, 2);
    set(
        sigilId,
        0,
        seatPulse(eliminationTarget->slot, count > 1, nowMs),
        false);
    return;
  }

  set(sigilId, breatheValue(nowMs), false, false);
}

void LedRenderer::renderGameOver(
    uint8_t sigilId,
    const Lobby &lobby,
    const GameEngine &game,
    uint32_t nowMs) {
  if (!game.moduleInGame(sigilId)) {
    off(sigilId);
    return;
  }

  uint8_t blue = 0;
  const PlayerSeat *winner = game.playerByNumber(game.winnerPlayerNumber());
  if (winner != nullptr && winner->moduleId == sigilId) {
    PlayerSeat local[2];
    const uint8_t count = game.playersForModule(sigilId, local, 2);
    blue = seatPulse(winner->slot, count > 1, nowMs) ? 255 : 0;
  }

  set(sigilId, blue, false, sigilId == lobby.hostModule());
}

void LedRenderer::render(
    HubState state,
    const Lobby &lobby,
    const GameEngine &game,
    uint32_t countdownStartedAtMs,
    uint8_t eliminationTargetPlayer,
    uint8_t winConfirmationPlayer,
    uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (!bus_.isOnline(id, nowMs)) {
      cache_[id].valid = false;
      continue;
    }

    switch (state) {
      case HubState::Lobby:
        renderLobby(id, lobby, nowMs);
        break;
      case HubState::Starting:
        renderStarting(id, lobby, countdownStartedAtMs, nowMs);
        break;
      case HubState::Running:
        renderRunning(id, game, nowMs);
        break;
      case HubState::Paused:
        renderPaused(
            id,
            game,
            eliminationTargetPlayer,
            winConfirmationPlayer,
            nowMs);
        break;
      case HubState::GameOver:
        renderGameOver(id, lobby, game, nowMs);
        break;
    }
  }
}

}  // namespace TurnHub
