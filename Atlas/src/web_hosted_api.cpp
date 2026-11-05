// Phone-hosted games over HTTP (protocol/hosted-v1.md): the hub claims the
// table, pushes each game state and reads Atlas's queued inputs. Atlas only
// stores and shows the hub's game; see hosted_game.h.

#include <stdlib.h>

#include "hosted_game.h"
#include "phone_links.h"
#include "profile_store.h"
#include "avatars.h"
#include "serial_log.h"
#include "turnhub_json.h"
#include "web_api_internal.h"

namespace TurnHubWebApi {
namespace internal {

namespace {

// Who hosts: a paired phone (its device key, phone_links.h) or a signed-in
// session. The lease keys on "phone:<id>" or the session token.
struct Hub {
  std::string token;
  std::string profileId;
  std::string name;
};

bool hubFor(WebServer &server, Hub &hub) {
  if (const TurnHubPhones::Phone *phone = TurnHubPhones::verify(server.header(TOKEN_HEADER).c_str())) {
    hub.token = std::string(TurnHubPhones::CREDENTIAL_PREFIX) + phone->id;
    hub.profileId.clear();
    hub.name = phone->name;
    return true;
  }
  const WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Pair this phone with Atlas first");
    return false;
  }
  hub.token = session->token;
  hub.profileId = session->profileId;
  hub.name.clear();
  if (session->profileId[0]) hub.name = TurnHubProfiles::nameForProfile(session->profileId).c_str();
  if (hub.name.empty()) {
    String name = server.arg("name");
    name.trim();
    hub.name = name.length() && name.length() <= turnhub::Game::MAX_NAME ? name.c_str() : "A phone";
  }
  return true;
}

void sendHolder(WebServer &server, int status, bool ok) {
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  turnhub::Json body = turnhub::Json::object();
  body.set("ok", ok);
  body.set("host", hosted.lease().held ? turnhub::Json(hosted.lease().name) : turnhub::Json());
  body.set("revision", hosted.hasGame() ? turnhub::Json(hosted.game().revision) : turnhub::Json());
  body.set("lastInput", static_cast<int64_t>(hosted.lastSeq()));
  body.set("leaseMs", static_cast<int64_t>(TurnHubHosted::LEASE_MS));
  sendJson(server, status, body.dump().c_str());
}

}  // namespace

void handleHostedClaim(WebServer &server) {
  Hub hub;
  if (!hubFor(server, hub)) return;
  const auto claim = TurnHubHosted::hostedTable().claim(hub.token, hub.profileId, hub.name, millis());
  sendHolder(server, claim == TurnHubHosted::Claim::Granted ? 200 : 409, claim == TurnHubHosted::Claim::Granted);
}

void handleHostedRelease(WebServer &server) {
  Hub hub;
  if (!hubFor(server, hub)) return;
  const bool released = TurnHubHosted::hostedTable().release(hub.token);
  sendHolder(server, released ? 200 : 409, released);
}

namespace {
// Profiles live on the phones (owner, 2026-11-05); Atlas keeps a copy of each
// profiled player the hub shows it (same profileId, the phone's name) so its
// statistics have someone to belong to. Only a new or renamed player is
// written: the hub re-sends its state every 10 s.
std::string copiedRoster;

void keepProfileCopies(const turnhub::Game &game) {
  std::string roster;
  for (const auto &p : game.players) {
    if (p.profileId.empty() || p.guest) continue;
    roster += p.profileId; roster += '\t'; roster += p.name; roster += '\n';
  }
  if (roster == copiedRoster) return;
  bool all = true;
  for (const auto &p : game.players) {
    if (p.profileId.empty() || p.guest || p.name.empty()) continue;
    const String id(p.profileId.c_str()), name(p.name.c_str());
    if (!TurnHubProfiles::profileExists(id)) all = TurnHubProfiles::createProfileWithName(name, id) == id && all;
    else if (TurnHubProfiles::nameForProfile(id) != name) all = TurnHubProfiles::setNameForProfile(id, name) && all;
  }
  if (!all) TurnHub::serialLog.printf("ATLAS|PROFILE_COPY|PARTIAL\n");
  if (all) copiedRoster = roster;
}
}  // namespace

// Phone-owned identity metadata only. Never accept credentials, PINs or account
// permissions as part of a copy. IDs, not names, join records to players.
void handlePlayerCopy(WebServer &server) {
  if (!TurnHubPhones::verify(server.header("X-TurnHub-Token").c_str())) {
    sendError(server, 401, "Pair this phone with Atlas first"); return;
  }
  const String id = server.arg("profileId"), name = server.arg("name");
  const String color = server.arg("color"), avatar = server.arg("avatar");
  uint32_t rgb = 0;
  bool validColor = color == "none" || (color.length() == 7 && color[0] == '#');
  if (color != "none") for (size_t i = 1; validColor && i < color.length(); ++i) {
    const char c = color[i];
    const int digit = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
    if (digit < 0) validColor = false; else rgb = (rgb << 4) | digit;
  }
  int32_t icon = 0;
  TurnHubIdentity::ProfileId parsed;
  if (!TurnHubIdentity::ProfileId::parse(id.c_str(), parsed) || !name.length() || name.length() > 32 ||
      !validColor || !parseBoundedNumber(avatar, 0, TurnHubAvatars::AVATAR_COUNT, icon)) {
    sendError(server, 400, "Supply a profile ID, name, color and preset avatar"); return;
  }
  if ((!TurnHubProfiles::profileExists(id) && TurnHubProfiles::createProfileWithName(name, id) != id) ||
      (TurnHubProfiles::nameForProfile(id) != name && !TurnHubProfiles::setNameForProfile(id, name))) {
    sendError(server, 503, "Atlas could not save the player copy; check its storage"); return;
  }
  uint32_t savedRgb = 0;
  const bool hadColor = TurnHubProfiles::jewelColorForProfile(id, savedRgb);
  const bool colorStored = (hadColor == (color != "none") && (!hadColor || savedRgb == rgb)) ||
      TurnHubProfiles::saveJewelColorForProfile(id, color != "none", rgb);
  const bool avatarStored = TurnHubProfiles::avatarForProfile(id) == icon ||
      TurnHubProfiles::saveAvatarForProfile(id, static_cast<uint8_t>(icon));
  // Identity and basic totals remain useful without SD; appearance can retry.
  sendJson(server, 200, String("{\"ok\":true,\"appearanceStored\":") +
      (colorStored && avatarStored ? "true}" : "false}"));
}

void handleHostedPublish(WebServer &server) {
  Hub hub;
  if (!hubFor(server, hub)) return;
  std::string error;
  if (!TurnHubHosted::hostedTable().publish(hub.token, server.arg("state").c_str(), millis(), error)) {
    sendError(server, TurnHubHosted::hostedTable().holds(hub.token, millis()) ? 400 : 409, error.c_str());
    return;
  }
  keepProfileCopies(TurnHubHosted::hostedTable().game());
  sendHolder(server, 200, true);
}

// Any paired phone or signed-in session may watch: the same facts the table screen shows.
void handleHostedState(WebServer &server) {
  Hub hub;
  if (!hubFor(server, hub)) return;
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  const uint32_t now = millis();
  String json = "{\"live\":";
  json += hosted.live(now) ? "true" : "false";
  json += ",\"host\":";
  json += hosted.lease().held ? String("\"") + jsonEscape(hosted.lease().name.c_str()) + "\"" : String("null");
  json += ",\"game\":";
  json += hosted.live(now) ? hosted.stateJson().c_str() : "null";
  // With no hub: the last hub's unfinished game, for this phone to resume.
  const std::string resume = hosted.resumeJson(now);
  json += ",\"resume\":";
  if (resume.empty()) {
    json += "null";
  } else {
    json += "{\"host\":\"";
    json += jsonEscape(hosted.resumeHost(now).c_str());
    json += "\",\"game\":";
    json += resume.c_str();
    json += "}";
  }
  json += "}";
  sendJson(server, 200, json);
}

void handleHostedInputs(WebServer &server) {
  Hub hub;
  if (!hubFor(server, hub)) return;
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  if (!hosted.holds(hub.token, millis())) {
    sendError(server, 409, "Another phone hosts this table");
    return;
  }
  const uint32_t after = static_cast<uint32_t>(strtoul(server.arg("after").c_str(), nullptr, 10));
  turnhub::Json list = turnhub::Json::array();
  for (const TurnHubHosted::Input &input : hosted.inputsAfter(after)) {
    turnhub::Json item = turnhub::Json::object();
    item.set("seq", static_cast<int64_t>(input.seq));
    item.set("id", input.id);
    item.set("player", static_cast<int64_t>(input.player));
    bool ok = false;
    item.set("action", turnhub::Json::parse(input.action, &ok));
    list.push(item);
  }
  turnhub::Json body = turnhub::Json::object();
  body.set("inputs", list);
  body.set("last", static_cast<int64_t>(hosted.lastSeq()));
  sendJson(server, 200, body.dump().c_str());
}

// POST name=<the phone's name>: a phone asks to pair (no sign-in). The Atlas
// screen shows "Allow this phone?" with the code; 409 while another phone is
// asking or eight are paired.
void handlePhonePair(WebServer &server) {
  const TurnHubPhones::Request *request = TurnHubPhones::ask(server.arg("name").c_str(), millis());
  if (!request) {
    sendError(server, 409, TurnHubPhones::count() >= TurnHubPhones::MAX_PHONES
        ? "Atlas has eight paired phones: forget them in Settings > Device first"
        : "Another phone is asking to pair. Try again in a minute");
    return;
  }
  char code[8];
  snprintf(code, sizeof(code), "%03lu %03lu", static_cast<unsigned long>(request->code / 1000),
      static_cast<unsigned long>(request->code % 1000));
  turnhub::Json body = turnhub::Json::object();
  body.set("request", std::string(request->id));
  body.set("code", std::string(code));
  body.set("waitMs", static_cast<int64_t>(TurnHubPhones::REQUEST_MS));
  sendJson(server, 200, body.dump().c_str());
}

// GET request=<id>: "waiting", "refused", "expired", "unknown", or "allowed"
// with the phone's ID and key, once. The phone then sends
// X-TurnHub-Token: phone:<id>:<key>.
void handlePhonePairPoll(WebServer &server) {
  char phoneId[TurnHubPhones::PHONE_ID_LENGTH + 1], key[TurnHubPhones::KEY_LENGTH + 1];
  const auto state = TurnHubPhones::collect(server.arg("request").c_str(), millis(), phoneId, key);
  static const char *const NAMES[] = {"waiting", "allowed", "refused", "expired", "unknown"};
  turnhub::Json body = turnhub::Json::object();
  body.set("state", std::string(NAMES[static_cast<uint8_t>(state)]));
  if (state == TurnHubPhones::RequestState::Allowed) {
    body.set("phone", std::string(phoneId));
    body.set("key", std::string(key));
    const TurnHubPhones::Phone *phone = TurnHubPhones::verify((std::string(TurnHubPhones::CREDENTIAL_PREFIX) + phoneId + ":" + key).c_str());
    body.set("owner", phone && phone->owner);
  }
  sendJson(server, 200, body.dump().c_str());
}

}  // namespace internal
}  // namespace TurnHubWebApi
