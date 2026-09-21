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
constexpr uint32_t BLUE_REFRESH_MS = 40;
}

LedRenderer::LedRenderer(SigilBus &bus)
    : bus_(bus) {
  invalidateAll();
}

void LedRenderer::invalidate(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) {
    cache_[sigilId].blueValid = false;
    cache_[sigilId].redValid = false;
    cache_[sigilId].greenValid = false;
    cache_[sigilId].displayValid = false;
  }
}

void LedRenderer::invalidateAll() {
  for (auto &entry : cache_) {
    entry.blueValid = false;
    entry.redValid = false;
    entry.greenValid = false;
    entry.displayValid = false;
  }
}

void LedRenderer::set(
    uint8_t sigilId,
    uint8_t blue,
    bool red,
    bool green,
    uint32_t nowMs) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) {
    return;
  }

  Cache &cache = cache_[sigilId];

  if (!cache.blueValid || cache.blue != blue) {
    // Smooth PWM values do not need millisecond transport updates. Cap them at
    // 25 Hz so animated breathing cannot outrun ESP-NOW. Hard endpoints remain
    // immediate because they are also used for flashes and state transitions.
    const bool endpoint = blue == 0 || blue == 255;
    const bool due =
        !cache.blueValid ||
        endpoint ||
        nowMs - cache.lastBlueTxMs >= BLUE_REFRESH_MS;

    if (due && bus_.setBlue(sigilId, blue)) {
      cache.blue = blue;
      cache.blueValid = true;
      cache.lastBlueTxMs = nowMs;
    }
  }

  if (!cache.redValid || cache.red != red) {
    if (bus_.setRed(sigilId, red)) {
      cache.red = red;
      cache.redValid = true;
    }
  }

  if (!cache.greenValid || cache.green != green) {
    if (bus_.setGreen(sigilId, green)) {
      cache.green = green;
      cache.greenValid = true;
    }
  }
}

void LedRenderer::off(uint8_t sigilId, uint32_t nowMs) {
  set(sigilId, 0, false, false, nowMs);
}

void LedRenderer::syncDisplay(
    uint8_t sigilId,
    HubState state,
    const Lobby &lobby,
    const GameEngine &game,
    uint8_t eliminationTargetPlayer,
    uint8_t winConfirmationPlayer) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) {
    return;
  }

  TurnHubProtocol::DisplayMode mode = TurnHubProtocol::DisplayMode::Ready;
  uint8_t primary = 0;
  uint8_t secondary = 0;
  uint8_t turnNumber = 0;
  uint8_t flags = 0;

  if (sigilId == lobby.hostController()) {
    flags |= TurnHubProtocol::DISPLAY_FLAG_HOST;
  }

  if (state == HubState::Lobby || state == HubState::Starting) {
    if (lobby.isJoined(sigilId)) {
      mode = state == HubState::Starting
          ? TurnHubProtocol::DisplayMode::Starting
          : TurnHubProtocol::DisplayMode::Lobby;

      primary = lobby.playerNumber(sigilId, 1);
      if (lobby.hasSecondary(sigilId)) {
        secondary = lobby.playerNumber(sigilId, 2);
      }

      PlayerSeat starter;
      if (lobby.selectedStarter(starter) && starter.controllerId == sigilId) {
        flags |= TurnHubProtocol::DISPLAY_FLAG_STARTER;
        if (starter.playerNumber == secondary && secondary != 0) {
          const uint8_t originalPrimary = primary;
          primary = secondary;
          secondary = originalPrimary;
        }
      }
    }
  } else if (game.controllerInGame(sigilId)) {
    switch (state) {
      case HubState::Running:
        mode = TurnHubProtocol::DisplayMode::Running;
        break;
      case HubState::Paused:
        mode = TurnHubProtocol::DisplayMode::Paused;
        break;
      case HubState::GameOver:
        mode = TurnHubProtocol::DisplayMode::GameOver;
        break;
      default:
        mode = TurnHubProtocol::DisplayMode::Running;
        break;
    }

    PlayerSeat local[2];
    const uint8_t count = game.playersForController(sigilId, local, 2);
    if (count > 0) {
      primary = local[0].playerNumber;
    }
    if (count > 1) {
      secondary = local[1].playerNumber;
    }

    const PlayerSeat *active = game.activePlayer();
    if ((state == HubState::Running || state == HubState::Paused) &&
        active != nullptr && active->controllerId == sigilId) {
      flags |= TurnHubProtocol::DISPLAY_FLAG_ACTIVE;
      if (active->playerNumber == secondary && secondary != 0) {
        const uint8_t originalPrimary = primary;
        primary = secondary;
        secondary = originalPrimary;
      }
    }

    const PlayerSeat *starter = game.playerByNumber(game.starterPlayerNumber());
    if (starter != nullptr && starter->controllerId == sigilId) {
      flags |= TurnHubProtocol::DISPLAY_FLAG_STARTER;
    }

    const PlayerSeat *winner = game.playerByNumber(game.winnerPlayerNumber());
    if (winner != nullptr && winner->controllerId == sigilId) {
      flags |= TurnHubProtocol::DISPLAY_FLAG_WINNER;
      if (state == HubState::GameOver &&
          winner->playerNumber == secondary && secondary != 0) {
        const uint8_t originalPrimary = primary;
        primary = secondary;
        secondary = originalPrimary;
      }
    }

    const PlayerSeat *elimination = game.playerByNumber(eliminationTargetPlayer);
    const PlayerSeat *confirmation = game.playerByNumber(winConfirmationPlayer);
    const PlayerSeat *attention = confirmation != nullptr ? confirmation : elimination;
    if (attention != nullptr && attention->controllerId == sigilId) {
      flags |= TurnHubProtocol::DISPLAY_FLAG_ATTENTION;
      if (attention->playerNumber == secondary && secondary != 0) {
        const uint8_t originalPrimary = primary;
        primary = secondary;
        secondary = originalPrimary;
      }
    }

    if (primary != 0) {
      const PlayerStats *stats = game.statsForPlayer(primary);
      if (stats != nullptr) {
        const uint32_t ordinal = stats->turnsCompleted + 1;
        turnNumber = static_cast<uint8_t>(ordinal > 255 ? 255 : ordinal);
      }
    }
  }

  const int32_t payload = TurnHubProtocol::encodeDisplayState(
      mode,
      primary,
      secondary,
      turnNumber,
      flags);

  Cache &cache = cache_[sigilId];
  if (cache.displayValid && cache.displayPayload == payload) {
    return;
  }

  if (bus_.send(sigilId, TurnHubProtocol::PacketType::DisplayState, payload)) {
    cache.displayPayload = payload;
    cache.displayValid = true;
  }
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
  if (!lobby.selectedStarter(starter) || starter.controllerId != sigilId) {
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
    set(sigilId, 255, false, false, nowMs);
  } else if (position < LOBBY_IDLE_LED_MS * 2) {
    set(sigilId, 0, false, true, nowMs);
  } else {
    set(sigilId, 0, true, false, nowMs);
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
      sigilId == lobby.hostController(),
      nowMs);
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
      false,
      nowMs);
}

void LedRenderer::renderRunning(
    uint8_t sigilId,
    const GameEngine &game,
    uint32_t nowMs) {
  if (!game.controllerInGame(sigilId)) {
    off(sigilId, nowMs);
    return;
  }

  if (sigilId != game.activeController()) {
    set(sigilId, 255, false, false, nowMs);
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

  set(sigilId, blue, red, green, nowMs);
}

void LedRenderer::renderPaused(
    uint8_t sigilId,
    const GameEngine &game,
    uint8_t eliminationTargetPlayer,
    uint8_t winConfirmationPlayer,
    uint32_t nowMs) {
  if (!game.controllerInGame(sigilId)) {
    off(sigilId, nowMs);
    return;
  }

  const PlayerSeat *winTarget = game.playerByNumber(winConfirmationPlayer);
  if (winTarget != nullptr && winTarget->controllerId == sigilId) {
    PlayerSeat living[2];
    const uint8_t count = game.livingPlayersForController(sigilId, living, 2);
    set(
        sigilId,
        0,
        false,
        seatPulse(winTarget->slot, count > 1, nowMs),
        nowMs);
    return;
  }

  const PlayerSeat *eliminationTarget = game.playerByNumber(eliminationTargetPlayer);
  if (eliminationTarget != nullptr && eliminationTarget->controllerId == sigilId) {
    PlayerSeat living[2];
    const uint8_t count = game.livingPlayersForController(sigilId, living, 2);
    set(
        sigilId,
        0,
        seatPulse(eliminationTarget->slot, count > 1, nowMs),
        false,
        nowMs);
    return;
  }

  set(sigilId, breatheValue(nowMs), false, false, nowMs);
}

void LedRenderer::renderGameOver(
    uint8_t sigilId,
    const Lobby &lobby,
    const GameEngine &game,
    uint32_t nowMs) {
  if (!game.controllerInGame(sigilId)) {
    off(sigilId, nowMs);
    return;
  }

  uint8_t blue = 0;
  const PlayerSeat *winner = game.playerByNumber(game.winnerPlayerNumber());
  if (winner != nullptr && winner->controllerId == sigilId) {
    PlayerSeat local[2];
    const uint8_t count = game.playersForController(sigilId, local, 2);
    blue = seatPulse(winner->slot, count > 1, nowMs) ? 255 : 0;
  }

  set(sigilId, blue, false, sigilId == lobby.hostController(), nowMs);
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
      cache_[id].blueValid = false;
      cache_[id].redValid = false;
      cache_[id].greenValid = false;
      cache_[id].displayValid = false;
      continue;
    }

    syncDisplay(
        id,
        state,
        lobby,
        game,
        eliminationTargetPlayer,
        winConfirmationPlayer);

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
