// Game endpoints: the v1 info/state client contract, seat listing, next-game
// settings, life and Commander counters, and the session control buttons.
// Mutations go through the callbacks registered by Atlas's web adapters.

#include <stdlib.h>

#include "firmware_version.h"
#include "protocol.h"
#include "web_api_internal.h"
#include "card_cache.h"
#include "controller_profiles.h"
#include "avatars.h"

namespace TurnHubWebApi {
namespace internal {

namespace {

// One counters request at a time on Atlas's application task. Neither radio
// callbacks nor SD workers access this disposable HTTP projection. Keep its
// large working set off the loop stack, including during WebServer::send.
CounterSnapshot counterWorkspace;
bool counterWorkspaceBusy = false;
struct CounterWorkspaceLease {
  CounterWorkspaceLease() { counterWorkspaceBusy = true; }
  ~CounterWorkspaceLease() { counterWorkspaceBusy = false; }
};

}  // namespace

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

namespace {

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

}  // namespace

// A positive decimal within [min, max].
bool parseBoundedNumber(const String &text, int32_t min, int32_t max, int32_t &value) {
  return parseLifeInteger(text, value, false) && value >= min && value <= max;
}

namespace {

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

}  // namespace

// Reports a seat callback's outcome: {"ok":true} or 409 with its message.
void sendSeatResult(WebServer &server, bool accepted, const String &message) {
  if (!accepted) {
    sendError(server, 409, message);
    return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

namespace {

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
  resolveAllSessions(millis());
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
      json += ",\"winner\":"; json += jsonBool(snapshot.winner);
      json += ",\"team\":"; json += snapshot.team ? String(snapshot.team) : String("null");
      json += ",\"lifeAvailable\":"; json += jsonBool(snapshot.lifeAvailable);
      json += ",\"life\":"; json += String(snapshot.life);
      json += ",\"profileId\":\""; json += jsonEscape(profileId);
      json += "\",\"name\":\""; json += jsonEscape(TurnHubProfiles::nameForProfile(profileId));
      const uint8_t avatar = TurnHubProfiles::avatarForProfile(profileId);
      json += "\",\"avatar\":";
      json += String(TurnHubAvatars::validPresetAvatar(avatar) ? avatar : 0);
    json += ",\"customAvatar\":\"" + jsonEscape(avatar == TurnHubAvatars::AVATAR_CUSTOM ? TurnHubProfiles::artworkPath(profileId) : String()) + "\"";
      json += ",\"hasPin\":"; json += jsonBool(hasPin(controllerId, slot));
      json += ",\"sessionClaimed\":"; json += jsonBool(seatHasSession(controllerId, slot));
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
  // Any seated player may edit next-game settings (no table host since 2026-09-25).
  const bool host = session && resolveSessionParticipant(*session) &&
      resolveSeatNow(session->controllerId, session->slot, seat) && seat.host;
  String presets;
  for (uint8_t i = 0; i < TurnHub::TURN_TIMER_PRESET_COUNT; ++i) {
    if (i) presets += ',';
    presets += String(TurnHub::TURN_TIMER_PRESETS_MS[i]);
  }
  const String json = String("{\"gameProfile\":\"") + TurnHub::gameProfileKey(settings.profile) +
      "\",\"startingLife\":" + String(settings.startingLife) +
      ",\"turnTimerMs\":" + String(settings.turnTimerMs) +
      ",\"autoEliminateAtZeroLife\":" + jsonBool(settings.autoEliminateAtZeroLife) +
      ",\"autoEliminateAtPoisonLimit\":" + jsonBool(settings.autoEliminateAtPoisonLimit) +
      ",\"autoEliminateAtCommanderDamage\":" + jsonBool(settings.autoEliminateAtCommanderDamage) +
      ",\"poisonLimit\":" + String(settings.poisonLimit) +
      ",\"twoHeadedGiant\":" + jsonBool(settings.twoHeadedGiant) +
      ",\"houseRules\":" + jsonBool(settings.houseRules) +
      ",\"planechase\":" + jsonBool(settings.planechase) +
      ",\"planarDeck\":\"" + (settings.planarDigital ? "digital" : "physical") + "\"" +
      ",\"teamLife\":{\"mtg\":" + String(TurnHub::TEAM_LIFE_MAGIC) +
      ",\"mtg_commander\":" + String(TurnHub::TEAM_LIFE_COMMANDER) + "}" +
      ",\"turnTimer\":{\"presetsMs\":[" + presets + "],\"minMs\":" + String(TurnHub::TURN_TIMER_MIN_MS) +
      ",\"maxMs\":" + String(TurnHub::TURN_TIMER_MAX_MS) +
      ",\"warningMs\":" + String(TurnHub::TURN_TIMER_WARNING_MS) +
      ",\"longTurnMs\":" + String(TurnHub::TURN_TIMER_LONG_TURN_MS) + "}" +
      ",\"available\":" + jsonBool(available) +
      ",\"canEdit\":" + jsonBool(available && editable && host) + "}";
  sendJson(server, 200, json);
}

bool parseGameSettings(WebServer &server, TurnHub::GameSettings &settings) {
  // Omitted fields keep their current value, so a client can change one setting.
  bool editable = false;
  if (readGameConfiguration) readGameConfiguration(settings, editable);
  const bool previousTeams = settings.twoHeadedGiant;
  if ((server.hasArg("gameProfile") &&
          !TurnHub::parseGameProfile(server.arg("gameProfile").c_str(), settings.profile)) ||
      (server.hasArg("startingLife") &&
          !parseLifeInteger(server.arg("startingLife"), settings.startingLife, false))) {
    sendError(server, 400, "Choose a valid game profile and starting life from 0 to 1000000");
    return false;
  }
  if (server.hasArg("turnTimerMs") && !parseTurnTimer(server.arg("turnTimerMs"), settings.turnTimerMs)) {
    sendError(server, 400, "Turn timer must be off or 15 seconds to 60 minutes in whole seconds");
    return false;
  }
  if (server.hasArg("twoHeadedGiant")) {
    const String teams = server.arg("twoHeadedGiant");
    if (teams != "0" && teams != "1") {
      sendError(server, 400, "twoHeadedGiant must be 0 or 1");
      return false;
    }
    settings.twoHeadedGiant = teams == "1";
  } else if (!TurnHub::teamsAllowed(settings.profile)) {
    // Switching to a profile without teams turns Two-Headed Giant off.
    settings.twoHeadedGiant = false;
  }
  if (server.hasArg("houseRules")) {
    const String value = server.arg("houseRules");
    if (value != "0" && value != "1") {
      sendError(server, 400, "houseRules must be 0 or 1");
      return false;
    }
    settings.houseRules = value == "1";
  }
  if (server.hasArg("planechase")) {
    const String value = server.arg("planechase");
    if (value != "0" && value != "1") {
      sendError(server, 400, "planechase must be 0 or 1");
      return false;
    }
    settings.planechase = value == "1";
  } else if (!TurnHub::magicRules(settings.profile)) {
    // Switching to a profile without Planechase turns it off.
    settings.planechase = false;
  }
  if (server.hasArg("planarDeck")) {
    const String deck = server.arg("planarDeck");
    if (deck != "physical" && deck != "digital") {
      sendError(server, 400, "planarDeck must be physical or digital");
      return false;
    }
    settings.planarDigital = deck == "digital";
  }
  if (server.hasArg("autoEliminateAtZeroLife")) {
    const String value = server.arg("autoEliminateAtZeroLife");
    if (value != "0" && value != "1") {
      sendError(server, 400, "autoEliminateAtZeroLife must be 0 or 1");
      return false;
    }
    settings.autoEliminateAtZeroLife = value == "1";
  }
  if (settings.twoHeadedGiant != previousTeams && !server.hasArg("poisonLimit")) settings.poisonLimit = settings.twoHeadedGiant ? 15 : 10;
  for (uint8_t i = 0; i < 2; ++i) {
    const char *key = i ? "autoEliminateAtCommanderDamage" : "autoEliminateAtPoisonLimit";
    if (!server.hasArg(key)) continue;
    const String value = server.arg(key);
    if (value != "0" && value != "1") { sendError(server, 400, "Elimination toggles must be 0 or 1"); return false; }
    (i ? settings.autoEliminateAtCommanderDamage : settings.autoEliminateAtPoisonLimit) = value == "1";
  }
  if (server.hasArg("poisonLimit") && !parseBoundedNumber(server.arg("poisonLimit"), 1, TurnHub::LIFE_LIMIT, settings.poisonLimit)) {
    sendError(server, 400, "Poison limit must be between 1 and 1000000"); return false;
  }
  return true;
}

void handleSaveGameSettings(WebServer &server) {
  WebSession *session = requireSession(server);
  if (!session) return;
  TurnHub::GameSettings settings;
  if (!parseGameSettings(server, settings)) return;
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
  if (counterWorkspaceBusy) {
    sendError(server, 503, "Counters response is busy");
    return;
  }
  CounterWorkspaceLease lease;
  CounterSnapshot &snapshot = counterWorkspace;
  if (!resolveSessionParticipant(*session) || !readCountersHandler ||
      !readCountersHandler(session->controllerId, session->slot, snapshot)) {
    sendJson(server, 200, "{\"available\":false,\"requests\":[],\"damage\":[]}");
    return;
  }
  if (snapshot.playerCount > MAX_PLAYERS) {
    sendError(server, 503, "Invalid counters snapshot");
    return;
  }
  String json;
  // Reserve for the actual bounded result, rather than the full 16-player
  // capacity on every phone's request. Each allowance includes worst-case
  // decimal lengths and separators.
  size_t capacity = 128;
  for (const auto &request : snapshot.requests) if (request.id) capacity += 160;
  if (snapshot.commanderEnabled) capacity += snapshot.playerCount * 96;
  if (!json.reserve(capacity)) {
    sendError(server, 503, "Could not allocate counters response");
    return;
  }
  json = "{\"available\":true,\"editable\":";
  json += jsonBool(snapshot.editable);
  json += ",\"commanderEnabled\":"; json += jsonBool(snapshot.commanderEnabled);
  json += ",\"player\":"; json += String(snapshot.player);
  json += ",\"requests\":[";
  bool first = true;
  const uint32_t now = millis();
  for (const auto &request : snapshot.requests) {
    if (!request.id) continue;
    if (!first) json += ',';
    first = false;
    const uint32_t age = now - request.requestedAtMs;
    const uint32_t remaining =
        request.state == TurnHub::LifeChangeState::Pending && age < request.windowMs
        ? request.windowMs - age : 0;
    json += "{\"id\":"; json += String(request.id);
    json += ",\"actor\":"; json += String(request.actor);
    json += ",\"target\":"; json += String(request.target);
    json += ",\"delta\":"; json += String(request.delta);
    json += ",\"state\":\""; json += lifeChangeStateName(request.state);
    json += "\",\"remainingMs\":"; json += String(remaining);
    json += ",\"windowMs\":"; json += String(request.windowMs);
    json += '}';
  }
  json += "],\"damage\":[";
  if (snapshot.commanderEnabled) {
    for (uint8_t i = 0; i < snapshot.playerCount; ++i) {
      if (i) json += ',';
      json += "{\"source\":"; json += String(snapshot.sources[i]);
      json += ",\"commanders\":["; json += String(snapshot.damage[i][0]);
      json += ','; json += String(snapshot.damage[i][1]);
      json += "]}";
    }
  }
  json += "]}";
  sendJson(server, 200, json);
}

// RequestLifeChange: delta + target. RespondLifeChange: requestId + accept.
// ChangeCounter: delta + source + commander.
bool freshGameActionRequest(WebServer &server) {
  if (!readClientRevision || server.arg("expectedBootId") != bootId ||
      server.arg("expectedRevision") != String(readClientRevision())) {
    sendError(server, 409, "The table changed. Review the current game and try again."); return false;
  }
  return true;
}

bool parseCounterPayload(WebServer &server, TurnHub::IntentType type, TurnHub::IntentPayload &payload) {
  if (type == TurnHub::IntentType::ResolveElimination) {
    int32_t victim = 0, source = 0;
    if (!parseBoundedNumber(server.arg("player"), 1, MAX_PLAYERS, victim) ||
        !parseBoundedNumber(server.arg("source"), 0, MAX_PLAYERS, source)) return false;
    const String self = server.arg("selfEliminated");
    if (self != "0" && self != "1") return false;
    payload.targetPlayer = static_cast<uint8_t>(victim); payload.eliminatedBy = static_cast<uint8_t>(source);
    payload.flags = self == "1" ? 1 : 0;
    return !payload.flags || !source;
  }
  if (type == TurnHub::IntentType::DoomBag) {
    const String cycle = server.arg("cycle");
    uint64_t value = 0;
    if (!cycle.length() || cycle.length() > 10) return false;
    for (size_t i = 0; i < cycle.length(); ++i) {
      if (cycle[i] < '0' || cycle[i] > '9') return false;
      value = value * 10 + cycle[i] - '0';
    }
    if (!value || value > UINT32_MAX) return false;
    payload.requestId = static_cast<uint32_t>(value);
    const String action = server.arg("action");
    if (action == "draw") return true;
    int32_t player = 0;
    if (!parseBoundedNumber(server.arg("player"), 1, MAX_PLAYERS, player)) return false;
    payload.targetPlayer = static_cast<uint8_t>(player);
    if (action == "eligibility") {
      payload.flags = 1; return parseBoundedNumber(server.arg("inBag"), 0, 1, payload.value);
    }
    if (action == "credit") {
      payload.flags = 2;
      if (!parseBoundedNumber(server.arg("source"), 1, MAX_PLAYERS, player)) return false;
      payload.counterSource = static_cast<uint8_t>(player); return true;
    }
    return false;
  }
  if (type == TurnHub::IntentType::PlanarAction) {
    using TurnHub::PlanarCommand;
    const String action = server.arg("action");
    if (action == "planeswalk") { payload.flags = static_cast<uint32_t>(PlanarCommand::Planeswalk); return true; }
    if (action == "chaos") { payload.flags = static_cast<uint32_t>(PlanarCommand::Chaos); return true; }
    if (action == "resolve") { payload.flags = static_cast<uint32_t>(PlanarCommand::Resolve); return true; }
    if (action == "top-to-bottom") { payload.flags = static_cast<uint32_t>(PlanarCommand::TopToBottom); return true; }
    if (action == "counter") {
      payload.flags = static_cast<uint32_t>(PlanarCommand::Counter);
      const String delta = server.arg("delta");
      payload.value = delta == "1" ? 1 : delta == "-1" ? -1 : 0;
      return payload.value != 0;
    }
    if (action != "roll") return false;
    payload.flags = static_cast<uint32_t>(PlanarCommand::Roll);
    const String face = server.arg("face"), effect = server.arg("fromEffect");
    payload.value = face == "blank" ? 0 : face == "chaos" ? 1 : face == "planeswalk" ? 2 : -1;
    if (effect.length() && effect != "0" && effect != "1") return false;
    payload.counterSlot = effect == "1" ? 1 : 0;
    return payload.value >= 0;
  }
  if (type == TurnHub::IntentType::RespondLifeChange) return parseLifeResponse(server, payload);
  int32_t number = 0;
  if (type == TurnHub::IntentType::ChangeCounter && server.hasArg("counter")) {
    const String kind = server.arg("counter");
    using TurnHub::CounterKind;
    // In CounterKind order; the app's counter names.
    const char *keys[] = {"", "poison", "energy", "experience", "commander-casts", "city-blessing", "initiative",
                          "treasure", "rad", "rad-resolve", "day-night", "turn-order", "everyone"};
    static_assert(sizeof(keys) / sizeof(keys[0]) == static_cast<size_t>(CounterKind::Count), "Counter names");
    for (uint8_t i = 1; i < static_cast<uint8_t>(CounterKind::Count); ++i) {
      if (kind != keys[i]) continue;
      payload.flags = i;
      const auto counter = static_cast<CounterKind>(i);
      if (counter == CounterKind::CityBlessing || counter == CounterKind::Initiative || counter == CounterKind::TurnOrder)
        return parseBoundedNumber(server.arg("value"), 0, 1, payload.value);
      if (counter == CounterKind::DayNight)
        return parseBoundedNumber(server.arg("value"), 0, static_cast<int32_t>(TurnHub::DayNight::Count) - 1, payload.value);
      // The nonland cards milled (0 just clears the reminder).
      if (counter == CounterKind::RadResolve) return parseBoundedNumber(server.arg("delta"), 0, TurnHub::LIFE_LIMIT, payload.value);
      if (counter == CounterKind::Everyone) {
        // kind: the counter every living player gains or loses one of.
        const String target = server.arg("kind");
        for (uint8_t k = 1; k < static_cast<uint8_t>(CounterKind::Count); ++k) {
          if (target != keys[k] || !TurnHub::everyoneCounter(static_cast<CounterKind>(k))) continue;
          payload.counterSlot = k;
          return parseLifeInteger(server.arg("delta"), payload.value, true) && (payload.value == 1 || payload.value == -1);
        }
        return false;
      }
      if (!parseLifeInteger(server.arg("delta"), payload.value, true) || !payload.value) return false;
      if (i == static_cast<uint8_t>(TurnHub::CounterKind::CommanderCast)) {
        if (!parseBoundedNumber(server.arg("commander"), 1, 2, number)) return false;
        payload.counterSlot = static_cast<uint8_t>(number);
      }
      return true;
    }
    return false;
  }
  bool valid = parseLifeInteger(server.arg("delta"), payload.value, true) && payload.value != 0;
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
  return valid;
}

// GET: the catalog's version and size, so the app knows when to send it.
// POST (any signed-in browser or tablet): version + planes, one line per card
// "id<TAB>name<TAB>0|1". Kept in RAM and on the microSD card.
void handlePlanesCatalogInfo(WebServer &server) {
  sendJson(server, 200, String("{\"version\":\"") + jsonEscape(TurnHubCards::planesVersion()) +
      "\",\"count\":" + String(TurnHubCards::planeCount()) + "}");
}

void handlePlanesCatalog(WebServer &server) {
  if (!requireSession(server)) return;
  bool saved = false;
  if (!TurnHubCards::replacePlanes(server.arg("planes").c_str(), server.arg("version").c_str(), saved)) {
    sendError(server, 400, "Send the planes as id, name and phenomenon lines with a version");
    return;
  }
  sendJson(server, 200, String("{\"ok\":true,\"count\":") + String(TurnHubCards::planeCount()) +
      ",\"saved\":" + jsonBool(saved) + "}");
}

// GET ?profileId= (a tablet, for a seated player) or the session's own profile.
void handleSavedCommanders(WebServer &server) {
  WebSession *session = requireSession(server);
  if (!session) return;
  const String profile = server.hasArg("profileId") ? server.arg("profileId") : String(session->profileId);
  TurnHubCards::SavedCommanders saved;
  if (!TurnHubProfiles::profileExists(profile) || !TurnHubCards::loadCommanders(profile, saved)) {
    sendError(server, 409, "Saved commanders need this Atlas's microSD card");
    return;
  }
  String body = "{\"profileId\":\"" + profile + "\",\"commanders\":[";
  for (uint8_t i = 0; i < saved.count; ++i) {
    if (i) body += ',';
    body += "{\"slot\":"; body += String(i + 1);
    body += ",\"name\":\""; body += jsonEscape(saved.slots[i].name);
    body += "\",\"art\":"; body += saved.slots[i].art[0] ? "\"" + jsonEscape(saved.slots[i].art) + "\"" : String("null");
    body += '}';
  }
  body += "]}";
  sendJson(server, 200, body);
}

// slot (1-based saved commander, 0 for none), or name (+ art) to save it to
// the seat's profile first. "forget" + slot removes a saved one.
void chooseCommanderFor(WebServer &server, uint8_t controllerId, uint8_t slot) {
  const String profile = TurnHubControllers::profileForSeat(controllerId, slot);
  if (!profile.length()) { sendError(server, 409, "Guests have no saved commanders"); return; }
  int32_t chosen = 0;
  if (server.hasArg("forget")) {
    if (!parseBoundedNumber(server.arg("forget"), 1, TurnHubCards::COMMANDER_SLOTS, chosen) ||
        !TurnHubCards::forgetCommander(profile, static_cast<uint8_t>(chosen))) {
      sendError(server, 409, "That saved commander isn't on this Atlas");
      return;
    }
    sendOkMessage(server, "Saved commander removed");
    return;
  }
  if (server.hasArg("name")) {
    if (!TurnHubCards::validCommander(server.arg("name"), server.arg("art"))) {
      sendError(server, 400, "Name a commander (up to 64 characters) and an optional printing ID");
      return;
    }
    chosen = TurnHubCards::rememberCommander(profile, server.arg("name"), server.arg("art"));
    if (!chosen) { sendError(server, 409, "Saving commanders needs this Atlas's microSD card"); return; }
  } else if (!parseBoundedNumber(server.arg("slot"), 0, TurnHubCards::COMMANDER_SLOTS, chosen)) {
    sendError(server, 400, "Choose a saved commander slot");
    return;
  }
  TurnHub::IntentPayload payload;
  payload.value = chosen;
  String message = "That player is not at the table";
  if (!counterControlHandler || !counterControlHandler(controllerId, slot, TurnHub::IntentType::SetCommander, payload, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

void handleCommanderChoice(WebServer &server) {
  WebSession *session = requireSession(server);
  if (!session) return;
  if (!resolveSessionParticipant(*session)) { sendError(server, 409, "Join the table first"); return; }
  chooseCommanderFor(server, session->controllerId, session->slot);
}

void handleCounterControl(WebServer &server, TurnHub::IntentType type) {
  WebSession *session = requireSession(server);
  if (!session) return;
  if ((type == TurnHub::IntentType::DoomBag || type == TurnHub::IntentType::ResolveElimination) && !freshGameActionRequest(server)) return;
  TurnHub::IntentPayload payload;
  if (!parseCounterPayload(server, type, payload)) {
    sendError(server, 400, "Invalid game action");
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
  runSeatControl(server, session->controllerId, session->slot, control);
}

void runSeatControl(WebServer &server, uint8_t controllerId, uint8_t slot, WebControl control) {
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
  bool accepted = false;
  if (control == WebControl::Start && server.arg("askEliminationSource") == "1") {
    TurnHub::IntentPayload payload; payload.flags = 1;
    accepted = counterControlHandler && counterControlHandler(controllerId, slot, TurnHub::IntentType::StartGame, payload, message);
  } else if (control == WebControl::EliminateSeat && server.hasArg("eliminatedBy")) {
    TurnHub::IntentPayload payload;
    int32_t source = 0;
    if (!parseBoundedNumber(server.arg("eliminatedBy"), 0, MAX_PLAYERS, source)) {
      sendError(server, 400, "Invalid credited player"); return;
    }
    payload.eliminatedBy = static_cast<uint8_t>(source);
    payload.flags = 2;  // Explicit Unknown is also an answered source.
    if (server.hasArg("selfEliminated")) {
      const String self = server.arg("selfEliminated");
      if ((self != "0" && self != "1") || (self == "1" && source)) { sendError(server, 400, "Invalid self elimination"); return; }
      payload.flags |= self == "1" ? 1 : 0;
    }
    accepted = counterControlHandler && counterControlHandler(controllerId, slot, TurnHub::IntentType::EliminateSeat, payload, message);
  } else {
    accepted = controlHandler(controllerId, slot, control, message);
  }
  if (!accepted) {
    sendControlResult(server, 409, "REJECTED",
        message.length() ? message : String("Control is not available right now"));
    return;
  }
  sendControlResult(server, 200, "ACCEPTED", message.length() ? message : String("Control accepted"));
}

}  // namespace internal
}  // namespace TurnHubWebApi
