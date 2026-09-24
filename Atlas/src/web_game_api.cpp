// Game endpoints: the v1 info/state client contract, seat listing, next-game
// settings, life and Commander counters, and the session control buttons.
// Mutations go through the callbacks registered by Atlas's web adapters.

#include <stdlib.h>

#include "firmware_version.h"
#include "protocol.h"
#include "web_api_internal.h"

namespace TurnHubWebApi {
namespace internal {

namespace {

// Parses a decimal life value or delta within +/-LIFE_LIMIT.
bool parseLifeInteger(const String &text, int32_t &value, bool negativeAllowed) {
  if (!text.length() || text.length() > 8) return false;
  const bool negative = text[0] == '-';
  size_t i = negative ? 1 : 0;
  if ((negative && !negativeAllowed) || i == text.length()) return false;
  uint32_t number = 0;
  for (; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    number = number * 10 + static_cast<uint32_t>(text[i] - '0');
    if (number > static_cast<uint32_t>(TurnHub::LIFE_LIMIT)) return false;
  }
  value = negative ? -static_cast<int32_t>(number) : static_cast<int32_t>(number);
  return true;
}

// Whole milliseconds, 0 (OFF) or within the Atlas range; Atlas validates again.
bool parseTurnTimer(const String &text, uint32_t &value) {
  if (!text.length() || text.length() > 7) return false;
  uint32_t number = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    number = number * 10 + static_cast<uint32_t>(text[i] - '0');
  }
  value = number;
  return TurnHub::validTurnTimerMs(number);
}

// A positive decimal within [min, max].
bool parseBoundedNumber(const String &text, int32_t min, int32_t max, int32_t &value) {
  return parseLifeInteger(text, value, false) && value >= min && value <= max;
}

const char *lifeChangeStateName(TurnHub::LifeChangeState state) {
  using TurnHub::LifeChangeState;
  switch (state) {
    case LifeChangeState::Pending: return "pending";
    case LifeChangeState::Accepted: return "accepted";
    case LifeChangeState::Rejected: return "rejected";
    case LifeChangeState::Automatic: return "automatic";
    case LifeChangeState::Cancelled: return "cancelled";
    case LifeChangeState::Failed: return "failed";
    default: return "none";
  }
}

// Sends 401 and returns null when the request has no valid session.
WebSession *requireSession(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) sendError(server, 401, "Sign in first");
  return session;
}

// Reports a seat callback's outcome: {"ok":true} or 409 with its message.
void sendSeatResult(WebServer &server, bool accepted, const String &message) {
  if (!accepted) {
    sendError(server, 409, message);
    return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

// Control responses carry the state revision so clients can tell whether
// their snapshot is still current.
void sendControlResult(WebServer &server, int httpStatus, const char *status, const String &message) {
  const bool ok = httpStatus == 200;
  String response = ok ? "{\"ok\":true,\"message\":\"" : "{\"ok\":false,\"error\":\"";
  response += jsonEscape(message);
  response += "\",\"status\":\""; response += status; response += '"';
  if (readClientRevision) {
    response += ",\"revision\":"; response += String(readClientRevision());
    response += ",\"bootId\":\""; response += bootId; response += '"';
  }
  response += '}';
  sendJson(server, httpStatus, response);
}

// Parses a RespondLifeChange request: requestId (1..UINT32_MAX) and accept=0|1.
bool parseLifeResponse(WebServer &server, TurnHub::IntentPayload &payload) {
  const String id = server.arg("requestId");
  const String accept = server.arg("accept");
  bool valid = id.length() > 0 && id.length() <= 10 && (accept == "0" || accept == "1");
  uint64_t parsed = 0;
  for (size_t i = 0; valid && i < id.length(); ++i) {
    valid = id[i] >= '0' && id[i] <= '9';
    if (valid) parsed = parsed * 10 + static_cast<uint8_t>(id[i] - '0');
  }
  payload.requestId = static_cast<uint32_t>(parsed);
  payload.flags = accept == "1" ? 1 : 0;
  return valid && parsed > 0 && parsed <= UINT32_MAX;
}

}  // namespace

void handleInfo(WebServer &server) {
  String json;
  json.reserve(640);
  json = "{\"product\":\"TurnHub\",\"deviceType\":\"Atlas\",\"atlasId\":\"";
  json += atlasHardwareId();
  json += "\",\"firmwareVersion\":\"";
  json += TurnHubFirmware::VERSION;
  json += "\",\"apiVersion\":\"1\",\"protocolVersion\":\"0.1\",\"radioProtocolVersion\":";
  json += String(TurnHubProtocol::VERSION);
  json += ",\"bootId\":\""; json += bootId;
  json += "\",\"revision\":"; json += String(readClientRevision ? readClientRevision() : 0);
  json += ",\"capabilities\":{\"stateSnapshot\":";
  json += jsonBool(readClientState && readClientRevision);
  json += ",\"sessionControls\":true,\"intentEnvelope\":false,\"events\":false,\"requestDeduplication\":false}}";
  sendJson(server, 200, json);
}

void handleState(WebServer &server) {
  if (!readClientState || !readClientRevision) {
    sendError(server, 503, "State snapshot is not configured");
    return;
  }
  sendJson(server, 200, readClientState(atlasHardwareId(), bootId));
}

void handleSeats(WebServer &server) {
  const uint32_t nowMs = millis();
  String json = "{\"seats\":[";
  json.reserve(4200);
  bool first = true;
  for (uint8_t controllerId = 0; controllerId < TurnHub::MAX_CONTROLLERS; ++controllerId) {
    for (uint8_t slot = 1; slot <= 2; ++slot) {
      SeatSnapshot snapshot;
      if (!resolveSeatNow(controllerId, slot, snapshot)) continue;
      if (!first) json += ',';
      first = false;

      const String profileId = profileIdForPhysicalSeat(controllerId, slot);
      json += "{\"module\":"; json += String(controllerId);
      json += ",\"virtual\":"; json += jsonBool(controllerId >= MAX_PHYSICAL_SIGILS);
      json += ",\"slot\":"; json += String(slot);
      json += ",\"slotName\":\""; json += slot == 1 ? "A" : "B";
      json += "\",\"player\":"; json += String(snapshot.playerNumber);
      json += ",\"active\":"; json += jsonBool(snapshot.active);
      json += ",\"eliminated\":"; json += jsonBool(snapshot.eliminated);
      json += ",\"lifeAvailable\":"; json += jsonBool(snapshot.lifeAvailable);
      json += ",\"life\":"; json += String(snapshot.life);
      json += ",\"profileId\":\""; json += jsonEscape(profileId);
      json += "\",\"name\":\""; json += jsonEscape(TurnHubProfiles::nameForProfile(profileId));
      json += "\",\"hasPin\":"; json += jsonBool(hasPin(controllerId, slot));
      json += ",\"sessionClaimed\":"; json += jsonBool(seatHasSession(controllerId, slot, nowMs));
      json += '}';
    }
  }
  json += "]}";
  sendJson(server, 200, json);
}

void handleGameSettings(WebServer &server) {
  TurnHub::GameSettings settings;
  bool editable = false;
  const bool available = readGameConfiguration && readGameConfiguration(settings, editable);
  WebSession *session = sessionForRequest(server);
  SeatSnapshot seat;
  // Only the host's primary seat may edit next-game settings.
  const bool host = session && resolveSessionParticipant(*session) &&
      resolveSeatNow(session->controllerId, session->slot, seat) && seat.host && session->slot == 1;
  String presets;
  for (uint8_t i = 0; i < TurnHub::TURN_TIMER_PRESET_COUNT; ++i) {
    if (i) presets += ',';
    presets += String(TurnHub::TURN_TIMER_PRESETS_MS[i]);
  }
  const String json = String("{\"gameProfile\":\"") + TurnHub::gameProfileKey(settings.profile) +
      "\",\"startingLife\":" + String(settings.startingLife) +
      ",\"turnTimerMs\":" + String(settings.turnTimerMs) +
      ",\"turnTimer\":{\"presetsMs\":[" + presets + "],\"minMs\":" + String(TurnHub::TURN_TIMER_MIN_MS) +
      ",\"maxMs\":" + String(TurnHub::TURN_TIMER_MAX_MS) +
      ",\"warningMs\":" + String(TurnHub::TURN_TIMER_WARNING_MS) +
      ",\"longTurnMs\":" + String(TurnHub::TURN_TIMER_LONG_TURN_MS) + "}" +
      ",\"available\":" + jsonBool(available) +
      ",\"canEdit\":" + jsonBool(available && editable && host) + "}";
  sendJson(server, 200, json);
}

void handleSaveGameSettings(WebServer &server) {
  WebSession *session = requireSession(server);
  if (!session) return;
  // Omitted fields keep their current value, so a client can change one setting.
  TurnHub::GameSettings settings;
  bool editable = false;
  if (readGameConfiguration) readGameConfiguration(settings, editable);
  if ((server.hasArg("gameProfile") &&
          !TurnHub::parseGameProfile(server.arg("gameProfile").c_str(), settings.profile)) ||
      (server.hasArg("startingLife") &&
          !parseLifeInteger(server.arg("startingLife"), settings.startingLife, false))) {
    sendError(server, 400, "Choose a valid game profile and starting life from 0 to 1000000");
    return;
  }
  if (server.hasArg("turnTimerMs") && !parseTurnTimer(server.arg("turnTimerMs"), settings.turnTimerMs)) {
    sendError(server, 400, "Turn timer must be off or 15 seconds to 60 minutes in whole seconds");
    return;
  }
  String message = "Join the table first";
  const bool accepted = resolveSessionParticipant(*session) && configureGameHandler &&
      configureGameHandler(session->controllerId, session->slot, settings, message);
  sendSeatResult(server, accepted, message);
}

void handleChangeLife(WebServer &server) {
  WebSession *session = requireSession(server);
  if (!session) return;
  int32_t delta = 0;
  if (!parseLifeInteger(server.arg("delta"), delta, true) || delta == 0) {
    sendError(server, 400, "Enter a nonzero life change between -1000000 and 1000000");
    return;
  }
  String message = "Join the table first";
  const bool accepted = resolveSessionParticipant(*session) && changeLifeHandler &&
      changeLifeHandler(session->controllerId, session->slot, delta, message);
  sendSeatResult(server, accepted, message);
}

void handleCounters(WebServer &server) {
  WebSession *session = requireSession(server);
  if (!session) return;
  CounterSnapshot snapshot;
  if (!resolveSessionParticipant(*session) || !readCountersHandler ||
      !readCountersHandler(session->controllerId, session->slot, snapshot)) {
    sendJson(server, 200, "{\"available\":false,\"requests\":[],\"damage\":[]}");
    return;
  }
  String json = String("{\"available\":true,\"editable\":") + jsonBool(snapshot.editable) +
      ",\"commanderEnabled\":" + jsonBool(snapshot.commanderEnabled) +
      ",\"player\":" + String(snapshot.player) + ",\"requests\":[";
  bool first = true;
  const uint32_t now = millis();
  for (const auto &request : snapshot.requests) {
    if (!request.id) continue;
    if (!first) json += ',';
    first = false;
    const uint32_t age = now - request.requestedAtMs;
    const uint32_t remaining =
        request.state == TurnHub::LifeChangeState::Pending && age < TurnHub::LIFE_APPROVAL_MS
        ? TurnHub::LIFE_APPROVAL_MS - age : 0;
    json += String("{\"id\":") + String(request.id) + ",\"actor\":" + String(request.actor) +
        ",\"target\":" + String(request.target) + ",\"delta\":" + String(request.delta) +
        ",\"state\":\"" + lifeChangeStateName(request.state) +
        "\",\"remainingMs\":" + String(remaining) + "}";
  }
  json += "],\"damage\":[";
  if (snapshot.commanderEnabled) {
    for (uint8_t i = 0; i < snapshot.playerCount; ++i) {
      if (i) json += ',';
      json += String("{\"source\":") + String(snapshot.sources[i]) + ",\"commanders\":[" +
          String(snapshot.damage[i][0]) + "," + String(snapshot.damage[i][1]) + "]}";
    }
  }
  json += "]}";
  sendJson(server, 200, json);
}

// RequestLifeChange: delta + target. RespondLifeChange: requestId + accept.
// ChangeCounter: delta + source + commander.
void handleCounterControl(WebServer &server, TurnHub::IntentType type) {
  WebSession *session = requireSession(server);
  if (!session) return;
  TurnHub::IntentPayload payload;
  int32_t number = 0;
  bool valid = true;
  if (type == TurnHub::IntentType::RespondLifeChange) {
    valid = parseLifeResponse(server, payload);
  } else {
    valid = parseLifeInteger(server.arg("delta"), payload.value, true) && payload.value != 0;
    if (type == TurnHub::IntentType::RequestLifeChange) {
      valid = valid && parseBoundedNumber(server.arg("target"), 1, MAX_PLAYERS, number);
      payload.targetPlayer = static_cast<uint8_t>(number);
    } else {
      valid = valid && parseBoundedNumber(server.arg("source"), 1, MAX_PLAYERS, number);
      payload.counterSource = static_cast<uint8_t>(number);
      valid = valid &&
          parseBoundedNumber(server.arg("commander"), 1, TurnHub::COMMANDERS_PER_PLAYER, number);
      payload.counterSlot = static_cast<uint8_t>(number);
    }
  }
  if (!valid) {
    sendError(server, 400, "Invalid life or Commander request");
    return;
  }
  String message = "Join the table first";
  if (!resolveSessionParticipant(*session) || !counterControlHandler ||
      !counterControlHandler(session->controllerId, session->slot, type, payload, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// Session controls (/api/control/*). An optional expectedRevision +
// expectedBootId makes the request conditional on the caller's snapshot.
void runControl(WebServer &server, WebControl control) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return;
  }
  SeatSnapshot snapshot;
  if (!resolveSessionParticipant(*session) ||
      !resolveSeatNow(session->controllerId, session->slot, snapshot)) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"Seat is no longer at the table\"}");
    return;
  }
  if (controlHandler == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Web controls are not configured\"}");
    return;
  }

  if (server.hasArg("expectedRevision")) {
    const String raw = server.arg("expectedRevision");
    char *end = nullptr;
    const unsigned long value = strtoul(raw.c_str(), &end, 10);
    if (!raw.length() || raw != String(value) || value > UINT32_MAX || !end || *end) {
      sendJson(server, 400, "{\"ok\":false,\"error\":\"Invalid expectedRevision\"}");
      return;
    }
    if (!readClientRevision || server.arg("expectedBootId") != bootId || value != readClientRevision()) {
      sendControlResult(server, 409, "CONFLICT", "Fetch a fresh state snapshot");
      return;
    }
  }

  String message;
  if (!controlHandler(session->controllerId, session->slot, control, message)) {
    sendControlResult(server, 409, "REJECTED",
        message.length() ? message : String("Control is not available right now"));
    return;
  }
  sendControlResult(server, 200, "ACCEPTED", message.length() ? message : String("Control accepted"));
}

}  // namespace internal
}  // namespace TurnHubWebApi
