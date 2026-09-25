// Menu Sigils: action availability and MenuState transport. See sigil_menu.h.
#include "sigil_menu.h"

#include "atlas_app.h"
#include "serial_log.h"
#include "web_api.h"
#include "controller_profiles.h"
#include "profile_store.h"

namespace TurnHubAtlas {

using TurnHubProtocol::MenuStateFields;
using TurnHubProtocol::SigilAction;

namespace {

struct MenuCache {
  bool computed = false;  // actions/defaultAction/revision describe the last menu.
  bool sent = false;      // That menu reached the Sigil (cleared to resend).
  uint32_t actions = 0;
  uint8_t defaultAction = TurnHubProtocol::SIGIL_ACTION_NONE;
  uint8_t revision = 0;
  // Life request shown on this Sigil (encodeLifeRequest; 0 = none).
  int32_t lifeRequest = 0;
  bool lifeSent = false;
  // SeatColor per seat (index 0 = A), resent with the menu.
  int32_t seatColor[2] = {0, 0};
  bool seatColorSent[2] = {false, false};
};

MenuCache menus[MAX_PHYSICAL_SIGILS];

// The action a click (compass center) or the list cursor starts on: the one
// the player most likely wants now.
constexpr SigilAction DEFAULT_ORDER[] = {
    SigilAction::ConfirmWin, SigilAction::Pass, SigilAction::Eliminate, SigilAction::Join,
    SigilAction::StartGame, SigilAction::Rematch, SigilAction::Resume, SigilAction::CancelPass,
    SigilAction::CancelStart, SigilAction::LinkPhone, SigilAction::CycleStarter,
    SigilAction::BeginElimination};

bool menuSigil(uint8_t sigilId) {
  const TurnHub::SigilRecord *record = sigilBus.record(sigilId);
  return record != nullptr && record->helloInfoValid &&
      (record->capabilities & TurnHubProtocol::CAPABILITY_MENU) != 0;
}

// Decodes MenuState2 (actions past the first 21, such as Leave).
bool menu2Sigil(uint8_t sigilId) {
  const TurnHub::SigilRecord *record = sigilBus.record(sigilId);
  return record != nullptr && record->helloInfoValid &&
      (record->capabilities & TurnHubProtocol::CAPABILITY_HARNESS) == 0 &&
      TurnHubProtocol::menuState2Firmware(record->firmwareMajor, record->firmwareMinor);
}

}  // namespace

MenuStateFields sigilMenuFor(uint8_t sigilId) {
  uint32_t actions = 0;
  const auto add = [&actions](SigilAction action) { actions |= TurnHubProtocol::sigilActionBit(action); };
  // No table host (owner decision 2026-09-25): every seated Sigil may start,
  // pick the starter, rematch or reset. Start keeps its cancellable countdown.
  const bool seated = lobby.isJoined(sigilId) || game.controllerInGame(sigilId);
  PlayerSeat living;
  const bool hasLivingSeat = firstLivingSeatForModule(sigilId, living);
  const PlayerSeat *active = game.activePlayer();
  const bool isActive = active != nullptr && active->controllerId == sigilId &&
      !game.isEliminated(active->playerNumber);

  switch (hubState) {
    case HubState::Lobby:
      if (!lobby.isJoined(sigilId)) {
        add(SigilAction::Join);
        break;
      }
      add(SigilAction::CycleStarter);
      if (lobby.hasSecondary(sigilId)) {
        add(SigilAction::RemoveSeatB);
      } else if (!sigilSeatsOnePlayer(sigilId)) {
        add(SigilAction::AddSeatB);
      }
      if (lobby.playerCount() >= 2) {
        add(SigilAction::StartGame);
        add(SigilAction::RandomStarter);
      }
      // Leave (both seats) fits only MenuState2.
      if (menu2Sigil(sigilId)) add(SigilAction::Leave);
      break;

    case HubState::Starting:
      // Any seated Sigil may cancel the countdown (handleCancelStartIntent).
      if (lobby.isJoined(sigilId)) add(SigilAction::CancelStart);
      break;

    case HubState::Running:
      if (isActive) {
        const bool passQueued = pendingPass.active && pendingPass.seat.controllerId == sigilId;
        add(passQueued ? SigilAction::CancelPass : SigilAction::Pass);
        add(SigilAction::ClaimWin);
      }
      if (hasLivingSeat) add(SigilAction::Pause);
      break;

    case HubState::Paused:
      if (game.hasWinClaim()) {
        const PlayerSeat *expected = game.playerByNumber(game.nextWinConfirmationPlayerNumber());
        if (expected != nullptr && expected->controllerId == sigilId) {
          add(SigilAction::ConfirmWin);
          add(SigilAction::DenyWin);
        }
      } else if (eliminationTargetPlayer != 0) {
        const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
        if (target != nullptr && target->controllerId == sigilId) {
          add(SigilAction::Eliminate);
          PlayerSeat seats[2];
          if (game.livingPlayersForController(sigilId, seats, 2) > 1) add(SigilAction::NextTarget);
        }
        // Cancelling is table-wide (handleEliminationIntent).
        if (game.controllerInGame(sigilId)) add(SigilAction::CancelElimination);
      } else if (hasLivingSeat) {
        add(SigilAction::Resume);
        add(SigilAction::BeginElimination);
        if (isActive) add(SigilAction::ClaimWin);
      }
      break;

    case HubState::GameOver:
      if (seated) {
        add(SigilAction::Rematch);
        add(SigilAction::ResetTable);
      }
      break;
  }

  if (TurnHubWebApi::hasPendingClaim(sigilId)) add(SigilAction::LinkPhone);
  // Left/Right life changes (MenuState2 Sigils), whenever ChangeLife could
  // succeed: a living seat in a running or paused game, no table decision.
  if (menu2Sigil(sigilId) && (hubState == HubState::Running || hubState == HubState::Paused) &&
      hasLivingSeat && !game.hasWinClaim() && eliminationTargetPlayer == 0) {
    add(SigilAction::AdjustLife);
  }

  MenuStateFields fields;
  fields.actions = actions;
  for (SigilAction action : DEFAULT_ORDER) {
    if ((actions & TurnHubProtocol::sigilActionBit(action)) != 0) {
      fields.defaultAction = static_cast<uint8_t>(action);
      break;
    }
  }
  return fields;
}

void syncSigilMenus(uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    MenuCache &cache = menus[id];
    if (!sigilBus.isOnline(id, nowMs) || !menuSigil(id)) {
      cache.sent = false;
      continue;
    }
    const MenuStateFields now = sigilMenuFor(id);
    if (!cache.computed || now.actions != cache.actions || now.defaultAction != cache.defaultAction) {
      if (cache.computed) {
        cache.revision = static_cast<uint8_t>((cache.revision + 1) & TurnHubProtocol::MENU_REVISION_MASK);
      }
      cache.computed = true;
      cache.actions = now.actions;
      cache.defaultAction = now.defaultAction;
      cache.sent = false;
    }
    if (cache.sent) continue;
    MenuStateFields fields = now;
    fields.revision = cache.revision;
    const bool sent = menu2Sigil(id)
        ? sigilBus.send(id, TurnHubProtocol::PacketType::MenuState2, TurnHubProtocol::encodeMenuState2(fields))
        : sigilBus.send(id, TurnHubProtocol::PacketType::MenuState, TurnHubProtocol::encodeMenuState(fields));
    if (sent) {
      cache.sent = true;
    }
  }
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    MenuCache &cache = menus[id];
    if (!sigilBus.isOnline(id, nowMs) || !menu2Sigil(id)) {
      cache.lifeSent = false;
      continue;
    }
    for (uint8_t slot = 1; slot <= 2; ++slot) {
      const int32_t color = sigilSeatColorFor(id, slot);
      if (color != cache.seatColor[slot - 1]) {
        cache.seatColor[slot - 1] = color;
        cache.seatColorSent[slot - 1] = false;
      }
      if (!cache.seatColorSent[slot - 1] &&
          sigilBus.send(id, TurnHubProtocol::PacketType::SeatColor, color)) {
        cache.seatColorSent[slot - 1] = true;
      }
    }
    const int32_t request = sigilLifeRequestFor(id);
    if (request != cache.lifeRequest) {
      cache.lifeRequest = request;
      cache.lifeSent = false;
    }
    if (!cache.lifeSent &&
        sigilBus.send(id, TurnHubProtocol::PacketType::LifeRequest, request)) {
      cache.lifeSent = true;
    }
  }
}

int32_t sigilSeatColorFor(uint8_t sigilId, uint8_t slot) {
  uint32_t rgb = 0;
  const String profile = TurnHubControllers::profileForSeat(sigilId, slot);
  const bool set = profile.length() > 0 && TurnHubProfiles::jewelColorForProfile(profile, rgb);
  return TurnHubProtocol::encodeSeatColor(slot, set, set ? rgb : 0);
}

int32_t sigilLifeRequestFor(uint8_t sigilId) {
  if (hubState != HubState::Running && hubState != HubState::Paused) return 0;
  PlayerSeat seats[2];
  const uint8_t count = game.livingPlayersForController(sigilId, seats, 2);
  for (uint8_t i = 0; i < count; ++i) {
    const TurnHub::LifeChangeRequest *request = game.lifeChangeFor(seats[i].playerNumber);
    if (request == nullptr || request->state != TurnHub::LifeChangeState::Pending) continue;
    TurnHubProtocol::LifeRequestFields f;
    f.target = request->target;
    f.requester = request->actor;
    f.tag = static_cast<uint8_t>(request->id & 0x3F);
    f.delta = request->delta;
    return TurnHubProtocol::encodeLifeRequest(f);
  }
  return 0;
}

void invalidateSigilMenu(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) {
    menus[sigilId].sent = false;
    menus[sigilId].lifeSent = false;
    menus[sigilId].seatColorSent[0] = menus[sigilId].seatColorSent[1] = false;
  }
}

uint8_t sigilMenuRevision(uint8_t sigilId) {
  return sigilId < MAX_PHYSICAL_SIGILS ? menus[sigilId].revision : 0;
}

void resetSigilMenus() {
  for (auto &cache : menus) cache = MenuCache{};
}

}  // namespace TurnHubAtlas
