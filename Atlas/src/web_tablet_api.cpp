// Tablet mode: one shared screen in the middle of the table that seats the
// players and acts for any of them, as the Atlas touchscreen does. A signed-in
// account turns it on with a table presence code; the grant lives on that
// browser session (RAM) until it leaves tablet mode, signs out or expires.
// Every action reuses the seat callbacks a phone uses, so Atlas's Intent
// handlers still decide (a seat changes only its own life, records only the
// Commander damage it received, and so on). See protocol/http-v1.md.

#include "web_api_internal.h"

#include "account_access.h"
#include "serial_log.h"

namespace TurnHubWebApi {
namespace internal {

using TurnHub::serialLog;

namespace {

// The session, or null after a 401 / 403 {"tabletRequired":true}.
WebSession *requireTablet(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return nullptr;
  }
  if (!session->tableDevice) {
    sendJson(server, 403,
        "{\"ok\":false,\"tabletRequired\":true,\"error\":\"Turn on tablet mode first\"}");
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

// True when another profile already uses this name (ignoring case).
bool nameTaken(const String &name) {
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][TurnHubProfiles::PROFILE_ID_LENGTH + 1];
  const size_t count = TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES);
  for (size_t i = 0; i < count; ++i) {
    if (TurnHubProfiles::nameForProfile(ids[i]).equalsIgnoreCase(name)) return true;
  }
  return false;
}

struct TabletAction {
  const char *name;
  WebControl control;
};

const TabletAction TABLET_ACTIONS[] = {
  {"pass", WebControl::Pass},
  {"pause", WebControl::PauseResume},
  {"concede", WebControl::Concede},
  {"win", WebControl::ClaimWin},
  {"confirm", WebControl::ConfirmWin},
  {"deny", WebControl::DenyWin},
  {"starter", WebControl::SelectStarter},
  {"start", WebControl::Start},
  {"cancel-start", WebControl::CancelStart},
  {"rematch", WebControl::Rematch},
  {"reset", WebControl::Reset},
};

}  // namespace

// POST /api/tablet/enable: needs table presence (POST /api/presence/request
// with purpose=tablet, then /api/presence/confirm).
void handleTabletEnable(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return;
  }
  if (!requirePhysicalPresence(server)) return;
  session->tableDevice = true;
  serialLog.print("ATLAS|TABLET|ON|");
  serialLog.println(session->profileId);
  sendOkMessage(server, "Tablet mode is on");
}

void handleTabletDisable(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return;
  }
  if (session->tableDevice) {
    serialLog.print("ATLAS|TABLET|OFF|");
    serialLog.println(session->profileId);
  }
  session->tableDevice = false;
  sendOkMessage(server, "Tablet mode is off");
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
    if (nameTaken(name)) {
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
    // The profile's "Allow Sigil and tablet use without a PIN" choice, as a
    // Sigil's picker applies it; otherwise its PIN, entered on the tablet.
    if (!physicalUseAllowed(profile)) {
      if (!server.hasArg("pin")) {
        sendJson(server, 403, String("{\"ok\":false,\"pinRequired\":true,\"error\":\"Enter ") +
            jsonEscape(TurnHubProfiles::nameForProfile(profile)) + "'s PIN\"}");
        return;
      }
      if (!verifyProfileSecret(server, profile, server.arg("pin"))) return;
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
  sendSeatResult(server, changeLifeHandler && changeLifeHandler(module, slot, delta, message), message);
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
  uint8_t module = 0, slot = 1;
  if (!tabletSeat(server, module, slot)) return;
  TurnHub::IntentPayload payload;
  if (!parseCounterPayload(server, type, payload)) {
    sendError(server, 400, "Invalid life or Commander request");
    return;
  }
  String message = "That player is not at the table";
  if (!counterControlHandler || !counterControlHandler(module, slot, type, payload, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

}  // namespace internal
}  // namespace TurnHubWebApi
