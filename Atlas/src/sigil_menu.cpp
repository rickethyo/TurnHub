// Sigil menus: action availability and MenuState2 transport. See sigil_menu.h.
#include "sigil_menu.h"

#include "profile_picker.h"

#include "atlas_app.h"
#include "serial_log.h"
#include "web_api.h"
#include "controller_profiles.h"
#include "profile_store.h"
#include "avatars.h"

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
  // StartingLife (0 outside a running or paused game).
  int32_t startingLife = 0;
  bool startingLifeSent = false;
  // PassPending: the passing player's number, 0 when no pass is pending.
  int32_t passing = 0;
  bool passingSent = false;
  // UpdateNotice (UpdateKind as a number).
  int32_t updateNotice = 0;
  bool updateNoticeSent = false;
};

MenuCache menus[MAX_PHYSICAL_SIGILS];

// The action a click (compass center) or the list cursor starts on: the one
// the player most likely wants now.
constexpr SigilAction DEFAULT_ORDER[] = {
    SigilAction::ConfirmWin, SigilAction::Pass, SigilAction::Eliminate, SigilAction::Join,
    SigilAction::StartGame, SigilAction::Rematch, SigilAction::Resume, SigilAction::CancelPass,
    SigilAction::CancelStart, SigilAction::LinkPhone, SigilAction::CycleStarter,
    SigilAction::BeginElimination};

// Seat colors come from profile records (Jewel color, avatar). Looking them up
// every loop pass cost a profile lookup and String churn per seat per pass, so
// they are re-read at this cadence; a change shows within it.
constexpr uint32_t SEAT_COLOR_REFRESH_MS = 500;
uint32_t lastSeatColorRefreshMs = 0;
bool seatColorsRead = false;

// A Sigil outside a game may take over a player in it who has no Sigil
// (seated from a phone or the tablet; owner request 2026-10-07).
bool canTakeOverPlayer(uint8_t sigilId) {
  if (table().game.controllerInGame(sigilId) || !pickerSigil(sigilId)) return false;
  for (uint8_t i = 0; i < table().game.playerCount(); ++i) {
    const PlayerSeat *seat = table().game.playerAt(i);
    if (seat != nullptr && seat->controllerId >= MAX_PHYSICAL_SIGILS) return true;
  }
  return false;
}

}  // namespace

MenuStateFields sigilMenuFor(uint8_t sigilId) {
  uint32_t actions = 0;
  const auto add = [&actions](SigilAction action) { actions |= TurnHubProtocol::sigilActionBit(action); };
  // No table host (owner decision 2026-09-25): every seated Sigil may start,
  // pick the starter, rematch or reset. Start keeps its cancelable countdown.
  const bool seated = table().lobby.isJoined(sigilId) || table().game.controllerInGame(sigilId);
  PlayerSeat living;
  const bool hasLivingSeat = firstLivingSeatForModule(sigilId, living);
  const bool isActive = table().game.turnSeatForController(sigilId) != nullptr;

  switch (table().hubState) {
    case HubState::Lobby:
      if (!table().lobby.isJoined(sigilId)) {
        add(SigilAction::Join);
        break;
      }
      add(SigilAction::CycleStarter);
      if (table().lobby.hasSecondary(sigilId)) {
        add(SigilAction::RemoveSeatB);
      } else {
        add(SigilAction::AddSeatB);
      }
      if (table().lobby.playerCount() >= 2) {
        add(SigilAction::StartGame);
        add(SigilAction::RandomStarter);
      }
      add(SigilAction::Leave);
      break;

    case HubState::Starting:
      // Any seated Sigil may cancel the countdown (handleCancelStartIntent).
      if (table().lobby.isJoined(sigilId)) add(SigilAction::CancelStart);
      break;

    case HubState::Running:
      // A Sigil outside the game may take over a player already in it.
      if (canTakeOverPlayer(sigilId)) add(SigilAction::Join);
      if (isActive) {
        // Any pass the team queued (Two-Headed Giant: either teammate's).
        const bool passQueued = table().pendingPass.active;
        add(passQueued ? SigilAction::CancelPass : SigilAction::Pass);
        add(SigilAction::ClaimWin);
      }
      if (hasLivingSeat) add(SigilAction::Pause);
      break;

    case HubState::Paused:
      if (canTakeOverPlayer(sigilId) && !table().game.hasWinClaim() && table().eliminationTargetPlayer == 0) {
        add(SigilAction::Join);
      }
      if (table().game.hasWinClaim()) {
        const PlayerSeat *expected = table().game.playerByNumber(table().game.nextWinConfirmationPlayerNumber());
        if (expected != nullptr && expected->controllerId == sigilId) {
          add(SigilAction::ConfirmWin);
          add(SigilAction::DenyWin);
        }
      } else if (table().eliminationTargetPlayer != 0) {
        const PlayerSeat *target = table().game.playerByNumber(table().eliminationTargetPlayer);
        if (target != nullptr && target->controllerId == sigilId) {
          add(SigilAction::Eliminate);
          PlayerSeat seats[2];
          if (table().game.livingPlayersForController(sigilId, seats, 2) > 1) add(SigilAction::NextTarget);
        }
        // Canceling is table-wide (handleEliminationIntent).
        if (table().game.controllerInGame(sigilId)) add(SigilAction::CancelElimination);
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
  // Left/Right life changes, whenever ChangeLife could
  // succeed: a living seat in a running or paused game, no table decision.
  if ((table().hubState == HubState::Running || table().hubState == HubState::Paused) &&
      hasLivingSeat && !table().game.hasWinClaim() && table().eliminationTargetPlayer == 0) {
    add(SigilAction::AdjustLife);
    if (table().game.settings().profile == TurnHub::GameProfile::Commander) {
      add(SigilAction::CommanderDamage);
      const uint8_t shown = leds.shownPlayer(sigilId,table().game);
      if (table().game.lastCommanderHit(shown)) add(SigilAction::UndoCommanderHit);
      if (!table().game.hasPartner(shown)) {
        add(SigilAction::AddPartner);
      } else {
        // Partners stay on once a second commander has dealt damage.
        bool dealt = false;
        for (uint8_t to = 1; to <= table().game.playerCount(); ++to) dealt = dealt || table().game.commanderDamage(to, shown, 2);
        if (!dealt) add(SigilAction::DropPartner);
      }
    }
  }
  // Two living seats on one Sigil: either one can be shown, and so have its
  // life changed, on any turn.
  PlayerSeat shared[2];
  if ((table().hubState == HubState::Running || table().hubState == HubState::Paused) &&
      table().game.livingPlayersForController(sigilId, shared, 2) == 2) {
    add(SigilAction::SwitchSeat);
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
    if (!sigilBus.isOnline(id, nowMs)) {
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
    if (sigilBus.send(id, TurnHubProtocol::PacketType::MenuState2, TurnHubProtocol::encodeMenuState2(fields))) {
      cache.sent = true;
    }
  }
  const bool refreshColors = !seatColorsRead || nowMs - lastSeatColorRefreshMs >= SEAT_COLOR_REFRESH_MS;
  if (refreshColors) {
    seatColorsRead = true;
    lastSeatColorRefreshMs = nowMs;
  }
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    MenuCache &cache = menus[id];
    if (!sigilBus.isOnline(id, nowMs)) {
      cache.lifeSent = false;
      cache.startingLifeSent = false;
      cache.passingSent = false;
      cache.updateNoticeSent = false;
      continue;
    }
    for (uint8_t slot = 1; slot <= 2 && refreshColors; ++slot) {
      const int32_t color = sigilSeatColorFor(id, slot);
      if (color != cache.seatColor[slot - 1]) {
        cache.seatColor[slot - 1] = color;
        cache.seatColorSent[slot - 1] = false;
      }
    }
    for (uint8_t slot = 1; slot <= 2; ++slot) {
      const int32_t color = cache.seatColor[slot - 1];
      if (!cache.seatColorSent[slot - 1] &&
          sigilBus.send(id, TurnHubProtocol::PacketType::SeatColor, color)) {
        cache.seatColorSent[slot - 1] = true;
      }
    }
    const int32_t startingLife = table().hubState == HubState::Running || table().hubState == HubState::Paused
        ? table().game.settings().startingLife : 0;
    if (startingLife != cache.startingLife) {
      cache.startingLife = startingLife;
      cache.startingLifeSent = false;
    }
    if (!cache.startingLifeSent &&
        sigilBus.send(id, TurnHubProtocol::PacketType::StartingLife, startingLife)) {
      cache.startingLifeSent = true;
    }
    const int32_t passing = table().pendingPass.active ? table().pendingPass.seat.playerNumber : 0;
    if (passing != cache.passing) {
      cache.passing = passing;
      cache.passingSent = false;
    }
    if (!cache.passingSent &&
        sigilBus.send(id, TurnHubProtocol::PacketType::PassPending, passing)) {
      cache.passingSent = true;
    }
    const int32_t update = static_cast<int32_t>(firmwareUpdateKind());
    if (update != cache.updateNotice) {
      cache.updateNotice = update;
      cache.updateNoticeSent = false;
    }
    if (!cache.updateNoticeSent &&
        sigilBus.send(id, TurnHubProtocol::PacketType::UpdateNotice, update)) {
      cache.updateNoticeSent = true;
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
  const uint8_t avatar = profile.length() ? TurnHubProfiles::avatarForProfile(profile) : 0;
  return TurnHubProtocol::encodeSeatColor(slot, set, set ? rgb : 0,
      TurnHubAvatars::validPresetAvatar(avatar) ? avatar : 0);
}

int32_t sigilLifeRequestFor(uint8_t sigilId) {
  if (table().hubState != HubState::Running && table().hubState != HubState::Paused) return 0;
  PlayerSeat seats[2];
  const uint8_t count = table().game.livingPlayersForController(sigilId, seats, 2);
  for (uint8_t i = 0; i < count; ++i) {
    const TurnHub::LifeChangeRequest *request = table().game.lifeChangeFor(seats[i].playerNumber);
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
    menus[sigilId].startingLifeSent = false;
    menus[sigilId].passingSent = false;
    menus[sigilId].updateNoticeSent = false;
  }
}

uint8_t sigilMenuRevision(uint8_t sigilId) {
  return sigilId < MAX_PHYSICAL_SIGILS ? menus[sigilId].revision : 0;
}

void resetSigilMenus() {
  for (auto &cache : menus) cache = MenuCache{};
  seatColorsRead = false;
}

}  // namespace TurnHubAtlas
