// Sigil menus: action availability and MenuState2 transport. See sigil_menu.h.
#include "sigil_menu.h"
#include "hosted_sigils.h"

#include "atlas_app.h"
#include "serial_log.h"
#include "web_api.h"
#include "hosted_game.h"
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
    SigilAction::CancelStart, SigilAction::CycleStarter,
    SigilAction::BeginElimination};

// Seat colors come from profile records (Jewel color, avatar). Looking them up
// every loop pass cost a profile lookup and String churn per seat per pass, so
// they are re-read at this cadence; a change shows within it.
constexpr uint32_t SEAT_COLOR_REFRESH_MS = 500;
uint32_t lastSeatColorRefreshMs = 0;
bool seatColorsRead = false;

}  // namespace

MenuStateFields sigilMenuFor(uint8_t sigilId) {
  // Phone-hosted games: the hub's game decides what this Sigil may do.
  MenuStateFields fields;
  if (hostedForSigil(sigilId, millis())) fields = hostedSigilMenu(sigilId);
  for (SigilAction action : DEFAULT_ORDER) {
    if ((fields.actions & TurnHubProtocol::sigilActionBit(action)) != 0) {
      fields.defaultAction = static_cast<uint8_t>(action);
      break;
    }
  }
  return fields;
}

void syncSigilMenus(uint32_t nowMs) {
  syncHostedPickers(nowMs);
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
    const int32_t startingLife = gameUnderway(nowMs) ? TurnHubHosted::hostedTable().game().startingLife : 0;
    if (startingLife != cache.startingLife) {
      cache.startingLife = startingLife;
      cache.startingLifeSent = false;
    }
    if (!cache.startingLifeSent &&
        sigilBus.send(id, TurnHubProtocol::PacketType::StartingLife, startingLife)) {
      cache.startingLifeSent = true;
    }
    const int32_t passing = 0;  // The hub passes at once; nothing waits here.
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
  // The hub's player on that seat, by their Atlas profile.
  uint32_t rgb = 0;
  String profile;
  if (TurnHubHosted::hostedTable().live(millis())) {
    const turnhub::Game &game = TurnHubHosted::hostedTable().game();
    const int index = TurnHubHosted::sigilPlayer(game, sigilId, slot);
    if (index >= 0) profile = game.players[index].profileId.c_str();
  }
  const bool set = profile.length() > 0 && TurnHubProfiles::jewelColorForProfile(profile, rgb);
  const uint8_t avatar = profile.length() ? TurnHubProfiles::avatarForProfile(profile) : 0;
  return TurnHubProtocol::encodeSeatColor(slot, set, set ? rgb : 0,
      TurnHubAvatars::validPresetAvatar(avatar) ? avatar : 0);
}

int32_t sigilLifeRequestFor(uint8_t) {
  return 0;  // The hub approves life changes on its own screen.
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
