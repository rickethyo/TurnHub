#include "led_renderer.h"

#include <cstring>
#include "profile_store.h"

namespace TurnHub {

namespace {
// How long a turn that just arrived counts as "TurnStarted" rather than "YourTurn".
constexpr uint32_t TURN_STARTED_CUE_MS = 3000;
constexpr uint32_t BLUE_REFRESH_MS = 40;

void setSeat(SigilLedState &cue, const GameEngine &game, uint8_t sigilId, const PlayerSeat &seat,
    bool livingOnly) {
  PlayerSeat local[2];
  const uint8_t count = livingOnly ? game.livingPlayersForController(sigilId, local, 2)
                                   : game.playersForController(sigilId, local, 2);
  cue.seatSlot = seat.slot;
  cue.sharedSeat = count > 1;
}

void addTimerOverlay(SigilLedState &cue, const GameEngine &game, uint32_t nowMs) {
  switch (game.turnTimerPhase(nowMs)) {
    case TurnTimerPhase::Warning: cue.overlays |= ledOverlayBit(LedOverlay::TurnWarning); break;
    case TurnTimerPhase::Expired: cue.overlays |= ledOverlayBit(LedOverlay::TimerExpired); break;
    case TurnTimerPhase::LongTurn: cue.overlays |= ledOverlayBit(LedOverlay::LongTurn); break;
    case TurnTimerPhase::Normal: break;
  }
}
}  // namespace

SigilLedState selectSigilLedState(
    uint8_t sigilId,
    HubState state,
    const Lobby &lobby,
    const GameEngine &game,
    uint32_t countdownStartedAtMs,
    uint8_t eliminationTargetPlayer,
    uint8_t winConfirmationPlayer,
    uint32_t nowMs) {
  SigilLedState cue;

  if (state == HubState::Lobby || state == HubState::Starting) {
    if (!lobby.isJoined(sigilId)) {
      cue.cue = LedCue::Unassigned;
      return cue;
    }
    if (state == HubState::Starting) {
      cue.cue = LedCue::Starting;
      cue.anchorMs = countdownStartedAtMs;
      return cue;
    }
    cue.cue = LedCue::Joined;
    cue.playerNumber = lobby.playerNumber(sigilId, 1);
    PlayerSeat starter;
    if (lobby.selectedStarter(starter) && starter.controllerId == sigilId) {
      cue.overlays |= ledOverlayBit(LedOverlay::Starter);
      cue.seatSlot = starter.slot;
      cue.sharedSeat = lobby.hasSecondary(sigilId);
    }
    return cue;
  }

  if (!game.controllerInGame(sigilId)) {
    return cue;  // Off.
  }

  if (state == HubState::Running) {
    if (sigilId != game.activeController()) {
      cue.cue = LedCue::Waiting;
      return cue;
    }
    const uint32_t turnElapsed = game.currentTurnElapsedMs(nowMs);
    cue.cue = turnElapsed < TURN_STARTED_CUE_MS ? LedCue::TurnStarted : LedCue::YourTurn;
    cue.anchorMs = nowMs - turnElapsed;
    addTimerOverlay(cue, game, nowMs);
    return cue;
  }

  if (state == HubState::Paused) {
    const PlayerSeat *confirm = game.playerByNumber(winConfirmationPlayer);
    const PlayerSeat *eliminate = game.playerByNumber(eliminationTargetPlayer);
    if (confirm != nullptr && confirm->controllerId == sigilId) {
      cue.cue = LedCue::ConfirmationNeeded;
      setSeat(cue, game, sigilId, *confirm, true);
    } else if (eliminate != nullptr && eliminate->controllerId == sigilId) {
      cue.cue = LedCue::EliminationSelect;
      setSeat(cue, game, sigilId, *eliminate, true);
    } else {
      cue.cue = LedCue::Paused;
    }
    return cue;
  }

  // Game over.
  cue.cue = LedCue::GameOver;
  const PlayerSeat *winner = game.playerByNumber(game.winnerPlayerNumber());
  if (winner != nullptr && winner->controllerId == sigilId) {
    cue.overlays |= ledOverlayBit(LedOverlay::Winner);
    setSeat(cue, game, sigilId, *winner, false);
  }
  return cue;
}

LedRenderer::LedRenderer(SigilBus &bus)
    : bus_(bus) {
  for (auto &profile : profiles_) profile = &defaultLedCueProfile();
  invalidateAll();
}

void LedRenderer::setProfile(const LedCueProfile &profile) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) setProfile(id, profile);
}

void LedRenderer::setProfile(uint8_t sigilId, const LedCueProfile &profile) {
  if (sigilId >= MAX_PHYSICAL_SIGILS || profiles_[sigilId] == &profile) return;
  profiles_[sigilId] = &profile;
  // Levels are recomputed every frame; cached channel values need no reset.
}

const LedCueProfile &LedRenderer::profile(uint8_t sigilId) const {
  return *profiles_[sigilId < MAX_PHYSICAL_SIGILS ? sigilId : 0];
}

void LedRenderer::invalidate(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) {
    cache_[sigilId].blueValid = false;
    cache_[sigilId].redValid = false;
    cache_[sigilId].greenValid = false;
    cache_[sigilId].ledStateValid = false;
    cache_[sigilId].displayValid = false;
  }
}

void LedRenderer::invalidateAll() {
  for (auto &entry : cache_) {
    entry.blueValid = false;
    entry.redValid = false;
    entry.greenValid = false;
    entry.ledStateValid = false;
    entry.displayValid = false;
  }
}

void LedRenderer::set(uint8_t sigilId, const LedLevels &levels, uint32_t nowMs) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) {
    return;
  }

  const uint8_t blue = levels.blue;
  const bool red = levels.red;
  const bool green = levels.green;
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

void LedRenderer::sendLedState(uint8_t sigilId, const SigilLedState &cue, uint32_t nowMs) {
  TurnHubProtocol::LedStateFields fields;
  fields.cue = cue.cue;
  fields.overlays = cue.overlays;
  fields.playerNumber = cue.playerNumber < 15 ? cue.playerNumber : 15;
  fields.seatSlot = cue.seatSlot;
  fields.sharedSeat = cue.sharedSeat;
  const LedCueProfile *profile = profiles_[sigilId];
  fields.style = profile == &reducedMotionLedCueProfile() ? TurnHubProtocol::LedStyle::ReducedMotion
      : profile == &monochromeSafeLedCueProfile() ? TurnHubProtocol::LedStyle::MonochromeSafe
      : TurnHubProtocol::LedStyle::Default;
  fields.anchorAgeMs = cue.anchorMs != 0 ? nowMs - cue.anchorMs : 0;

  const int32_t value = TurnHubProtocol::encodeLedState(fields);
  const uint32_t key = TurnHubProtocol::ledStateKey(value);
  Cache &cache = cache_[sigilId];
  if (cache.ledStateValid && cache.ledStateKey == key) return;
  if (bus_.send(sigilId, TurnHubProtocol::PacketType::LedState, value)) {
    cache.ledStateValid = true;
    cache.ledStateKey = key;
  }
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
  const auto *record = bus_.record(sigilId);
  if (mode == TurnHubProtocol::DisplayMode::Running && record &&
      (record->capabilities & TurnHubProtocol::CAPABILITY_GAME_DISPLAY)) {
    TurnHubProtocol::GameDisplayPacket snapshot{};
    snapshot.version = TurnHubProtocol::VERSION;
    snapshot.type = TurnHubProtocol::PacketType::GameDisplay;
    snapshot.sigilId = sigilId;
    snapshot.state = payload;
    snapshot.commander = game.settings().profile == GameProfile::Commander;
    auto name = [&](uint8_t player, char *out) {
      const auto *seat = game.playerByNumber(player);
      if (!seat) return;
      // Names are stable within a captured game participant. Re-read on the
      // existing Hello/profile/lifecycle invalidation, not every LED frame.
      const char *cached = nullptr;
      if (cache.displayValid && cache.gameDisplay.type == TurnHubProtocol::PacketType::GameDisplay) {
        const auto &old = cache.gameDisplay;
        if (player == TurnHubProtocol::displayPrimaryPlayer(old.state)) cached = old.primary.name;
        else if (player == TurnHubProtocol::displaySecondaryPlayer(old.state)) cached = old.secondary.name;
        else for (uint8_t i = 0; i < old.sourceCount; ++i)
          if (old.sources[i].player == player) cached = old.sources[i].name;
      }
      if (cached) {
        memcpy(out, cached, TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1);
        return;
      }
      const String value = TurnHubProfiles::nameForProfile(String(seat->profileId));
      if (!value.length()) {
        snprintf(out, TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1, "Player %u", player);
        return;
      }
      for (size_t i = 0; i < value.length() && i < TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH; ++i)
        out[i] = value[i] >= 32 && value[i] <= 126 ? value[i] : '?';
    };
    snapshot.primary.life = game.lifeTotal(primary);
    name(primary, snapshot.primary.name);
    if (secondary) {
      snapshot.secondary.life = game.lifeTotal(secondary);
      name(secondary, snapshot.secondary.name);
    }
    // Deterministic player-number order; received by the focused local seat.
    if (snapshot.commander) for (uint8_t source = 1; source <= MAX_PLAYERS; ++source) {
      if (source == primary || !game.playerByNumber(source)) continue;
      const int32_t a = game.commanderDamage(primary, source, 1);
      const int32_t b = game.commanderDamage(primary, source, 2);
      if (!a && !b) continue;
      if (snapshot.sourceCount == TurnHubProtocol::DISPLAY_COMMANDER_SOURCES) {
        ++snapshot.omittedSources;
        continue;
      }
      auto &entry = snapshot.sources[snapshot.sourceCount++];
      entry.player = source;
      name(source, entry.name);
      entry.damage[0] = a;
      entry.damage[1] = b;
    }
    if (!cache.displayValid || memcmp(&snapshot, &cache.gameDisplay, sizeof(snapshot)) != 0) {
      if (bus_.sendGameDisplay(snapshot)) {
        cache.gameDisplay = snapshot;
        cache.displayPayload = payload;
        cache.displayValid = true;
      }
    }
    return;
  }
  if (cache.displayValid && cache.displayPayload == payload) {
    return;
  }

  if (bus_.send(sigilId, TurnHubProtocol::PacketType::DisplayState, payload)) {
    cache.displayPayload = payload;
    cache.displayValid = true;
  }
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
      invalidate(id);
      continue;
    }

    syncDisplay(
        id,
        state,
        lobby,
        game,
        eliminationTargetPlayer,
        winConfirmationPlayer);

    const SigilLedState cue = selectSigilLedState(
        id, state, lobby, game, countdownStartedAtMs,
        eliminationTargetPlayer, winConfirmationPlayer, nowMs);
    const SigilRecord *record = bus_.record(id);
    if (record != nullptr && record->helloInfoValid &&
        (record->capabilities & TurnHubProtocol::CAPABILITY_LED_STATE) != 0) {
      sendLedState(id, cue, nowMs);
    } else {
      set(id, ledLevels(*profiles_[id], cue, nowMs), nowMs);
    }
  }
}

}  // namespace TurnHub
