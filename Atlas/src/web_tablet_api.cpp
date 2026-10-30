// Shared seat controls: any signed-in profile can use them directly.
// Each action reuses Atlas's existing authoritative Intent handlers.
// Legacy shared-display activation remains for older clients only.

#include "web_api_internal.h"

#include "account_access.h"
#include "serial_log.h"

namespace TurnHubWebApi {
namespace internal {

using TurnHub::serialLog;

namespace {

// A signed-in session; there is no tablet permission or activation gate.
WebSession *requireTablet(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return nullptr;
  }
  return session;
}

// The seat a tablet acts for: module and slot, as /api/v1/state's players
// report them. Sends 400 when they are malformed; Atlas checks the seat.
bool tabletSeat(WebServer &server, uint8_t &module, uint8_t &slot) {
  int32_t m = 0, s = 0;
  if (!parseBoundedNumber(server.arg("module"), 0, TurnHub::MAX_CONTROLLERS - 1, m) ||
      !parseBoundedNumber(server.arg("slot"), 1, 2, s)) {
    sendError(server, 400, "Choose a player");
    return false;
  }
  module = static_cast<uint8_t>(m);
  slot = static_cast<uint8_t>(s);
  return true;
}

struct TabletAction {
  const char *name;
  WebControl control;
};

const TabletAction TABLET_ACTIONS[] = {
  {"pass", WebControl::Pass},
  {"pause", WebControl::PauseResume},
  {"concede", WebControl::Concede},
  {"eliminate", WebControl::EliminateSeat},
  {"monarch", WebControl::ClaimMonarch},
  {"win", WebControl::ClaimWin},
  {"confirm", WebControl::ConfirmWin},
  {"deny", WebControl::DenyWin},
  {"starter", WebControl::SelectStarter},
  {"start", WebControl::Start},
  {"cancel-start", WebControl::CancelStart},
  {"rematch", WebControl::Rematch},
  {"reset", WebControl::Reset},
  {"back-to-setup", WebControl::BackToSetup},
  {"count-game", WebControl::CountShortGame},
  {"move-earlier", WebControl::MoveEarlier},
  {"move-later", WebControl::MoveLater},
};

// A change the tablet made while Atlas was out of reach carries `queuedMs`,
// how long ago it was tapped (Atlas has no wall clock). It is applied now,
// under today's rules, like any other change; the age only goes in the log.
void logOfflineChange(WebServer &server, const char *kind, uint8_t module, uint8_t slot, bool applied) {
  int32_t ageMs = 0;
  if (!server.hasArg("queuedMs") || !parseBoundedNumber(server.arg("queuedMs"), 0, 86400000, ageMs)) return;
  serialLog.print("ATLAS|TABLET|OFFLINE|");
  serialLog.print(kind);
  serialLog.print("|seat=");
  serialLog.print(static_cast<unsigned>(module));
  serialLog.print('/');
  serialLog.print(static_cast<unsigned>(slot));
  serialLog.print("|delta=");
  serialLog.print(server.arg("delta"));
  serialLog.print("|ageMs=");
  serialLog.print(static_cast<long>(ageMs));
  serialLog.println(applied ? "|applied" : "|refused");
}

}  // namespace

// Legacy shared-display session endpoint; signed-in clients can use table actions directly.
void handleTabletEnable(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return;
  }
  // Replace this device's personal credential with a RAM-only tablet account.
  // Other devices' sessions and the player's seat remain independent.
  const uint8_t game = requestGame(server);
  *session = WebSession{};
  session->used = true;
  session->tableDevice = true;
  session->table = game;
  session->lastSeenMs = millis();
  makeToken(session->token);
  serialLog.println("ATLAS|TABLET|ON");
  sendJson(server, 200, String("{\"ok\":true,\"token\":\"") + session->token +
      "\",\"profileId\":\"\",\"message\":\"Shared tablet account active; personal account signed out\"}");
}

void handleTabletDisable(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return;
  }
  *session = WebSession{};
  serialLog.println("ATLAS|TABLET|OFF");
  sendOkMessage(server, "Tablet mode is off; sign in to a personal account to continue");
}

// POST /api/tablet/seat: profileId (with pin when that profile needs one at
// the table), or name to create a profile with no PIN. Then joins the lobby.
void handleTabletSeat(WebServer &server) {
  if (!requireTablet(server)) return;
  if (!profileStoreReady) {
    sendError(server, 503, "Profile storage unavailable");
    return;
  }
  String profile;
  if (server.hasArg("name")) {
    const String name = cleanName(server.arg("name"));
    if (name.length() == 0) {
      sendError(server, 400, "Enter the player's name");
      return;
    }
    if (profileNameTaken(name)) {
      sendError(server, 409, name + " already has a profile; choose them from the list");
      return;
    }
    profile = TurnHubProfiles::createProfileWithName(name);
    if (profile.length() == 0) {
      sendError(server, 503, "Could not create the profile; storage may be full");
      return;
    }
    serialLog.print("ATLAS|TABLET|NEW_PROFILE|");
    serialLog.println(profile);
  } else {
    profile = server.arg("profileId");
    if (!TurnHubProfiles::profileExists(profile)) {
      sendError(server, 404, "Unknown profile");
      return;
    }

  }
  String message;
  if (!profileControlHandler ||
      !profileControlHandler(profile, WebControl::Join, INVALID_ID, 1, message)) {
    sendError(server, 409, message);
    return;
  }
  sendJson(server, 200, String("{\"ok\":true,\"profileId\":\"") + profile +
      "\",\"message\":\"" + jsonEscape(message) + "\"}");
}

// POST /api/tablet/unseat: profileId leaves the lobby (and any Sigil seat).
void handleTabletUnseat(WebServer &server) {
  if (!requireTablet(server)) return;
  String message;
  if (!profileControlHandler ||
      !profileControlHandler(server.arg("profileId"), WebControl::Leave, INVALID_ID, 1, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// POST /api/tablet/control: module, slot, action (pass, pause, concede, win,
// confirm, deny, starter, start, cancel-start, rematch, reset).
void handleTabletControl(WebServer &server) {
  if (!requireTablet(server)) return;
  uint8_t module = 0, slot = 1;
  if (!tabletSeat(server, module, slot)) return;
  const String action = server.arg("action");
  for (const auto &entry : TABLET_ACTIONS) {
    if (action == entry.name) {
      runSeatControl(server, module, slot, entry.control);
      return;
    }
  }
  sendError(server, 400, "Unknown action");
}

// POST /api/tablet/life: module, slot, delta (the seat's own life).
void handleTabletLife(WebServer &server) {
  if (!requireTablet(server)) return;
  uint8_t module = 0, slot = 1;
  if (!tabletSeat(server, module, slot)) return;
  int32_t delta = 0;
  if (!parseLifeInteger(server.arg("delta"), delta, true) || delta == 0) {
    sendError(server, 400, "Enter a nonzero life change between -1000000 and 1000000");
    return;
  }
  String message = "That player is not at the table";
  const bool applied = changeLifeHandler && changeLifeHandler(module, slot, delta, message);
  logOfflineChange(server, "life", module, slot, applied);
  sendSeatResult(server, applied, message);
}

// POST /api/tablet/settings: module and slot of any seated player, then the
// fields /api/game/settings takes (lobby only, as for a phone).
void handleTabletSettings(WebServer &server) {
  if (!requireTablet(server)) return;
  uint8_t module = 0, slot = 1;
  if (!tabletSeat(server, module, slot)) return;
  TurnHub::GameSettings settings;
  if (!parseGameSettings(server, settings)) return;
  String message = "That player is not at the table";
  sendSeatResult(server, configureGameHandler && configureGameHandler(module, slot, settings, message), message);
}

// POST /api/tablet/commander (delta, source, commander: damage the seat
// received) and /api/tablet/life/respond (requestId, accept: a phone's life
// request to that seat).
void handleTabletCounter(WebServer &server, TurnHub::IntentType type) {
  if (!requireTablet(server)) return;
  if ((type == TurnHub::IntentType::DoomBag || type == TurnHub::IntentType::ResolveElimination) && !freshGameActionRequest(server)) return;
  uint8_t module = 0, slot = 1;
  if (!tabletSeat(server, module, slot)) return;
  TurnHub::IntentPayload payload;
  if (!parseCounterPayload(server, type, payload)) {
    sendError(server, 400, "Invalid game action");
    return;
  }
  String message = "That player is not at the table";
  const bool applied = counterControlHandler && counterControlHandler(module, slot, type, payload, message);
  if (type == TurnHub::IntentType::ChangeCounter) logOfflineChange(server, "commander", module, slot, applied);
  if (!applied) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// POST /api/tablet/commander-choice: module, slot, then as /api/control/commander.
void handleTabletCommanderChoice(WebServer &server) {
  if (!requireTablet(server)) return;
  uint8_t module = 0, slot = 1;
  if (!tabletSeat(server, module, slot)) return;
  chooseCommanderFor(server, module, slot);
}

}  // namespace internal

bool profileNameTaken(const String &name) {
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][TurnHubProfiles::PROFILE_ID_LENGTH + 1];
  const size_t count = TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES);
  for (size_t i = 0; i < count; ++i) {
    if (TurnHubProfiles::nameForProfile(ids[i]).equalsIgnoreCase(name)) return true;
  }
  return false;
}

}  // namespace TurnHubWebApi
