// Phone-hosted games over HTTP (protocol/hosted-v1.md): the hub claims the
// table, pushes each game state and reads Atlas's queued inputs. Atlas only
// stores and shows the hub's game; see hosted_game.h.

#include <stdlib.h>

#include "hosted_game.h"
#include "profile_store.h"
#include "turnhub_json.h"
#include "web_api_internal.h"

namespace TurnHubWebApi {
namespace internal {

namespace {

const WebSession *hubSession(WebServer &server) {
  const WebSession *session = sessionForRequest(server);
  if (!session) sendError(server, 401, "Sign in first");
  return session;
}

std::string hubName(WebServer &server, const WebSession &session) {
  if (session.profileId[0]) {
    const String name = TurnHubProfiles::nameForProfile(session.profileId);
    if (name.length()) return name.c_str();
  }
  String name = server.arg("name");
  name.trim();
  if (name.length() && name.length() <= turnhub::Game::MAX_NAME) return name.c_str();
  return session.tableDevice ? "Table tablet" : "A phone";
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
  const WebSession *session = hubSession(server);
  if (!session) return;
  const auto claim = TurnHubHosted::hostedTable().claim(session->token, session->profileId, hubName(server, *session), millis());
  sendHolder(server, claim == TurnHubHosted::Claim::Granted ? 200 : 409, claim == TurnHubHosted::Claim::Granted);
}

void handleHostedRelease(WebServer &server) {
  const WebSession *session = hubSession(server);
  if (!session) return;
  const bool released = TurnHubHosted::hostedTable().release(session->token);
  sendHolder(server, released ? 200 : 409, released);
}

void handleHostedPublish(WebServer &server) {
  const WebSession *session = hubSession(server);
  if (!session) return;
  std::string error;
  if (!TurnHubHosted::hostedTable().publish(session->token, server.arg("state").c_str(), millis(), error)) {
    sendError(server, TurnHubHosted::hostedTable().holds(session->token, millis()) ? 400 : 409, error.c_str());
    return;
  }
  sendHolder(server, 200, true);
}

// Anyone signed in may watch: the same facts the table screen shows.
void handleHostedState(WebServer &server) {
  if (!hubSession(server)) return;
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  const uint32_t now = millis();
  String json = "{\"live\":";
  json += hosted.live(now) ? "true" : "false";
  json += ",\"host\":";
  json += hosted.lease().held ? String("\"") + jsonEscape(hosted.lease().name.c_str()) + "\"" : String("null");
  json += ",\"game\":";
  json += hosted.live(now) ? hosted.stateJson().c_str() : "null";
  json += "}";
  sendJson(server, 200, json);
}

void handleHostedInputs(WebServer &server) {
  const WebSession *session = hubSession(server);
  if (!session) return;
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  if (!hosted.holds(session->token, millis())) {
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

}  // namespace internal
}  // namespace TurnHubWebApi
