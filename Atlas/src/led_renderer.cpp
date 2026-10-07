#include "led_renderer.h"

#include <cstring>
#include "profile_store.h"

namespace TurnHub {

namespace {
// How long a turn that just arrived counts as "TurnStarted" rather than "YourTurn".
constexpr uint32_t TURN_STARTED_CUE_MS = 3000;

void setSeat(SigilLedState &cue, const GameEngine &game, uint8_t sigilId, const PlayerSeat &seat,
    bool livingOnly) {
  PlayerSeat local[2];
  const uint8_t count = livingOnly ? game.livingPlayersForController(sigilId, local, 2)
                                   : game.playersForController(sigilId, local, 2);
  cue.seatSlot = seat.slot;
  cue.sharedSeat = count > 1;
}

// The winning seat on this Sigil (Two-Headed Giant: either teammate), or nullptr.
const PlayerSeat *winningSeatForController(const GameEngine &game, uint8_t sigilId) {
  const PlayerSeat *winner = game.playerByNumber(game.winnerPlayerNumber());
  if (winner != nullptr && winner->controllerId == sigilId) return winner;
  const PlayerSeat *mate = game.playerByNumber(game.teammateOf(game.winnerPlayerNumber()));
  return mate != nullptr && mate->controllerId == sigilId ? mate : nullptr;
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
    // Two-Headed Giant: both teammates' Sigils show the team's turn.
    const PlayerSeat *turnSeat = game.turnSeatForController(sigilId);
    if (turnSeat == nullptr) {
      cue.cue = LedCue::Waiting;
      return cue;
    }
    const uint32_t turnElapsed = game.currentTurnElapsedMs(nowMs);
    cue.cue = turnElapsed < TURN_STARTED_CUE_MS ? LedCue::TurnStarted : LedCue::YourTurn;
    cue.anchorMs = nowMs - turnElapsed;
    // Which seat's turn, so a shared Sigil can show A and B differently.
    setSeat(cue, game, sigilId, *turnSeat, true);
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
  const PlayerSeat *winner = winningSeatForController(game, sigilId);
  if (winner != nullptr) {
    cue.overlays |= ledOverlayBit(LedOverlay::Winner);
    setSeat(cue, game, sigilId, *winner, false);
  }
  return cue;
}

LedRenderer::LedRenderer(SigilBus &bus)
    : bus_(bus) {
  for (auto &style : styles_) style = TurnHubProtocol::LedStyle::Default;
  invalidateAll();
}

void LedRenderer::setStyle(uint8_t sigilId, TurnHubProtocol::LedStyle style) {
  if (sigilId < MAX_PHYSICAL_SIGILS) styles_[sigilId] = style;
}

TurnHubProtocol::LedStyle LedRenderer::style(uint8_t sigilId) const {
  return styles_[sigilId < MAX_PHYSICAL_SIGILS ? sigilId : 0];
}
uint8_t LedRenderer::shownPlayer(uint8_t sigilId, const GameEngine &game) const {
  if (sigilId >= MAX_PHYSICAL_SIGILS) return 0;
  if (cache_[sigilId].displayValid) return TurnHubProtocol::displayPrimaryPlayer(cache_[sigilId].displayPayload);
  if (focus_[sigilId].player) return focus_[sigilId].player;
  if (const PlayerSeat *turn = game.turnSeatForController(sigilId)) return turn->playerNumber;
  PlayerSeat seats[2];
  return game.livingPlayersForController(sigilId,seats,2) ? seats[0].playerNumber : 0;
}
bool LedRenderer::switchShownSeat(uint8_t sigilId, const GameEngine &game) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) return false;
  PlayerSeat seats[2];
  if (game.livingPlayersForController(sigilId, seats, 2) < 2) return false;
  const Cache &cache = cache_[sigilId];
  const uint8_t shown = cache.displayValid
      ? TurnHubProtocol::displayPrimaryPlayer(cache.displayPayload)
      : seats[0].playerNumber;
  focus_[sigilId].player = shown == seats[0].playerNumber ? seats[1].playerNumber : seats[0].playerNumber;
  focus_[sigilId].activeWhenChosen = game.activePlayerNumber();
  cache_[sigilId].displayValid = false;
  return true;
}

void LedRenderer::invalidate(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) {
    cache_[sigilId].ledStateValid = false;
    cache_[sigilId].clockValid = false;
    cache_[sigilId].displayValid = false;
  }
}

void LedRenderer::invalidateAll() {
  for (auto &entry : cache_) {
    entry.ledStateValid = false;
    entry.clockValid = false;
    entry.displayValid = false;
  }
}

void LedRenderer::sendLedState(uint8_t sigilId, const SigilLedState &cue, uint32_t nowMs) {
  TurnHubProtocol::LedStateFields fields;
  fields.cue = cue.cue;
  fields.overlays = cue.overlays;
  fields.playerNumber = cue.playerNumber < 15 ? cue.playerNumber : 15;
  fields.seatSlot = cue.seatSlot;
  fields.sharedSeat = cue.sharedSeat;
  fields.style = styles_[sigilId];

  fields.anchorAgeMs = cue.anchorMs != 0 ? nowMs - cue.anchorMs : 0;

  const int32_t value = TurnHubProtocol::encodeLedState(fields);
  const uint32_t key = TurnHubProtocol::ledStateKey(value);
  Cache &cache = cache_[sigilId];
  // Atlas's clock paces every Sigil's looping patterns (TableClock).
  if ((!cache.clockValid || nowMs - cache.lastClockTxMs >= TurnHubProtocol::TABLE_CLOCK_INTERVAL_MS) &&
      bus_.send(sigilId, TurnHubProtocol::PacketType::TableClock, static_cast<int32_t>(nowMs))) {
    cache.clockValid = true;
    cache.lastClockTxMs = nowMs;
  }
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
    focus_[sigilId] = SeatFocus();
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

    const PlayerSeat *active = game.turnSeatForController(sigilId);
    const bool activeHere = (state == HubState::Running || state == HubState::Paused) &&
        active != nullptr;
    if (activeHere) {
      flags |= TurnHubProtocol::DISPLAY_FLAG_ACTIVE;
      if (active->playerNumber == secondary && secondary != 0) {
        const uint8_t originalPrimary = primary;
        primary = secondary;
        secondary = originalPrimary;
      }
    }

    // Shared seats: the seat chosen with Switch seat stays shown until the
    // turn comes round to this Sigil again (playtest 2026-09-29, item 3).
    SeatFocus &focus = focus_[sigilId];
    const uint8_t activeNumber = game.activePlayerNumber();
    if (focus.player != 0 &&
        ((activeHere && activeNumber != focus.activeWhenChosen) || secondary == 0 ||
         (focus.player != primary && focus.player != secondary) || game.isEliminated(focus.player))) {
      focus = SeatFocus();
    }
    if (focus.player != 0 && focus.player == secondary) {
      secondary = primary;
      primary = focus.player;
    }
    // Never lead with an eliminated seat while the other one still plays.
    if (secondary != 0 && game.isEliminated(primary) && !game.isEliminated(secondary)) {
      const uint8_t originalPrimary = primary;
      primary = secondary;
      secondary = originalPrimary;
    }

    const PlayerSeat *starter = game.playerByNumber(game.starterPlayerNumber());
    if (starter != nullptr && starter->controllerId == sigilId) {
      flags |= TurnHubProtocol::DISPLAY_FLAG_STARTER;
    }

    const PlayerSeat *winner = winningSeatForController(game, sigilId);
    if (winner != nullptr) {
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

    // The table round, as on the Atlas screen. It was the shown player's own
    // next-turn count, which ran ahead of the table after their turn and
    // jumped when a shared Sigil switched seats (playtest 2026-09-29, item 4).
    if (primary != 0) {
      const uint16_t round = game.currentRound();
      turnNumber = static_cast<uint8_t>(round > 255 ? 255 : round);
    }
  }

  const PlayerSeat *focused = game.playerByNumber(primary);
  const bool primaryIsB = (state == HubState::Lobby || state == HubState::Starting) ?
      primary == lobby.playerNumber(sigilId, 2) : (focused && focused->slot == 2);
  if (secondary && primaryIsB) flags |= TurnHubProtocol::DISPLAY_FLAG_PRIMARY_B;

  const int32_t payload = TurnHubProtocol::encodeDisplayState(
      mode,
      primary,
      secondary,
      turnNumber,
      flags);

  Cache &cache = cache_[sigilId];
  if (mode == TurnHubProtocol::DisplayMode::Running) {
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
    // The payload check catches a return from a DisplayState-only mode
    // (Paused) even when the game snapshot itself did not change.
    if (!cache.displayValid || cache.displayPayload != payload ||
        memcmp(&snapshot, &cache.gameDisplay, sizeof(snapshot)) != 0) {
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
    uint32_t nowMs,
    uint16_t sigilMask) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if ((sigilMask & (1u << id)) == 0) continue;
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
    sendLedState(id, cue, nowMs);
  }
}

}  // namespace TurnHub
