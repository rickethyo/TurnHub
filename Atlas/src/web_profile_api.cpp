// Profile endpoints: the sign-in list, registration, table participation,
// name/PIN edits, the per-profile policy and statistics.

#include "account_access.h"
#include "profile_statistics.h"
#include "web_api_internal.h"

namespace TurnHubWebApi {
namespace internal {

namespace {

using TurnHubProfiles::ProfileStats;

constexpr size_t EXPORT_NAME_MAX_CHARS = 24;

uint32_t averageMs(uint64_t total, uint32_t count) {
  return count == 0 ? 0 : static_cast<uint32_t>(total / count);
}

String uint64Text(uint64_t value) {
  char text[32];
  snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
  return String(text);
}

// Percentage with one decimal place, e.g. "42.9%".
String winRateText(const ProfileStats &stats) {
  const uint32_t tenths = stats.gamesPlayed == 0
      ? 0
      : static_cast<uint32_t>((static_cast<uint64_t>(stats.gamesWon) * 1000ULL) / stats.gamesPlayed);
  char text[16];
  snprintf(text, sizeof(text), "%lu.%lu%%",
      static_cast<unsigned long>(tenths / 10), static_cast<unsigned long>(tenths % 10));
  return String(text);
}

// A filesystem-safe download name derived from the player's name.
String safeExportName(const String &name, const String &profileId) {
  String safe;
  safe.reserve(40);
  for (size_t i = 0; i < name.length() && safe.length() < EXPORT_NAME_MAX_CHARS; ++i) {
    const char c = name[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_') {
      safe += c;
    } else if (c == ' ' && safe.length() > 0) {
      safe += '_';
    }
  }
  if (safe.length() == 0) {
    safe = "profile_";
    safe += profileId;
  }
  safe += "_turnhub_stats.txt";
  return safe;
}

// Loads the signed-in profile's statistics or sends the error response.
WebSession *loadSessionStats(WebServer &server, String &profileId, String &name, ProfileStats &stats) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return nullptr;
  }
  profileId = sessionProfileId(*session);
  if (profileId.length() == 0) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"This session is not attached to a durable profile\"}");
    return nullptr;
  }
  name = TurnHubProfiles::nameForProfile(profileId);
  if (!TurnHubProfiles::loadStatsForProfile(profileId, stats)) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Profile statistics storage unavailable\"}");
    return nullptr;
  }
  return session;
}

// "moderation": the private history for a PIN-verified owner only; otherwise
// just the reason it is hidden. Counts never appear for anyone else.
String moderationJson(const WebSession &session, const String &profileId) {
  if (!session.pinVerified) {
    return "{\"visible\":false,\"reason\":\"Sign in with your PIN to see your private moderation history\"}";
  }
  TurnHubProfiles::ModerationStats moderation;
  if (!TurnHubProfiles::loadModerationStatsForProfile(profileId, moderation)) {
    return "{\"visible\":false,\"reason\":\"Moderation history is unavailable\"}";
  }
  return String("{\"visible\":true,\"connectionResets\":") + String(moderation.connectionResets) +
      ",\"gameRemovals\":" + String(moderation.gameRemovals) + "}";
}

bool hasFreeSessionSlot() {
  cleanup(millis());
  for (const auto &session : sessions) {
    if (!session.used) return true;
  }
  return false;
}

// Logs out every other browser of this profile after a PIN change.
void endOtherSessions(const WebSession &current, const String &profileId) {
  for (auto &other : sessions) {
    if (&other != &current && other.used && String(other.profileId) == profileId) other = WebSession{};
  }
}

}  // namespace

void handleProfiles(WebServer &server) {
  if (!profileStoreReady) {
    sendError(server, 503, "Profile storage unavailable");
    return;
  }
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][TurnHubProfiles::PROFILE_ID_LENGTH + 1];
  const size_t count = TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES);
  String json = "{\"profiles\":[";
  bool comma = false;
  for (size_t i = 0; i < count; ++i) {
    TurnHubAccounts::Account account;
    if (!TurnHubAccounts::load(String(ids[i]), account) || account.archived) continue;
    if (comma) json += ',';
    comma = true;
    json += "{\"profileId\":\""; json += ids[i];
    json += "\",\"name\":\""; json += jsonEscape(TurnHubProfiles::nameForProfile(ids[i]));
    json += "\",\"hasPin\":"; json += jsonBool(TurnHubProfiles::hasPinForProfile(ids[i]));
    json += '}';
  }
  json += "]}";
  sendJson(server, 200, json);
}

void handleRegistration(WebServer &server) {
  String name = server.arg("name");
  name.trim();
  const String pin = server.arg("pin");
  if (name.length() == 0 || name.length() > MAX_NAME_LENGTH || !validPin(pin)) {
    sendError(server, 400, "A name (1-32 characters) and a 4-8 digit PIN are required");
    return;
  }
  // Check before creating so a full session table does not orphan a profile.
  if (!hasFreeSessionSlot()) {
    sendError(server, 503, "No session slots available");
    return;
  }
  const String id = TurnHubProfiles::createProfileWithCredentials(name, pin, profilePinHash);
  if (id.length() == 0) {
    sendError(server, 503, "Could not create profile; storage may be full");
    return;
  }
  // The new profile's PIN was just entered, so the session is PIN-verified.
  sendLogin(server, createProfileSession(id, millis(), true));
}

void handleParticipation(WebServer &server, WebControl control) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in to a profile first");
    return;
  }
  String message;
  if (!profileControlHandler ||
      !profileControlHandler(sessionProfileId(*session), control, INVALID_ID, 1, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// Updates the signed-in profile: any of name, pin, clearPin=1.
void handleProfile(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return;
  }

  const String profileId = sessionProfileId(*session);
  const bool clearPin = server.hasArg("clearPin") && server.arg("clearPin") == "1";
  if (server.hasArg("clearPin")) {
    // Privileged and moderation-reset accounts must keep a PIN.
    TurnHubAccounts::Account account;
    if (!TurnHubAccounts::load(profileId, account) || account.permissions || account.reconnectRequired) {
      sendError(server, 403, "This account must keep a PIN");
      return;
    }
  }
  if (!profileStoreReady || profileId.length() == 0) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Profile storage unavailable\"}");
    return;
  }

  resolveSessionParticipant(*session);
  const SigilRecord *record = recordForModule(session->controllerId);
  bool displayProfileChanged = false;

  if (server.hasArg("name")) {
    if (!TurnHubProfiles::setNameForProfile(profileId, cleanName(server.arg("name")))) {
      sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save player name\"}");
      return;
    }
    displayProfileChanged = true;
  }

  if (server.hasArg("pin")) {
    const String pin = server.arg("pin");
    if (!validPin(pin)) {
      sendJson(server, 400, "{\"ok\":false,\"error\":\"PIN must be 4 to 8 digits\"}");
      return;
    }
    const String hash = profilePinHash(profileId, pin);
    if (hash.length() != PIN_HASH_LENGTH || !TurnHubProfiles::setPinHashForProfile(profileId, hash)) {
      sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save PIN\"}");
      return;
    }
    if (record != nullptr) TurnHubProfiles::setPinHashForSeat(record->mac, session->slot, hash);
    session->pinVerified = true;  // It now knows the profile's PIN.
  }

  if (clearPin) {
    TurnHubProfiles::ProfilePolicy policy;
    if (!TurnHubProfiles::loadPolicyForProfile(profileId, policy) || !policy.allowPhysicalWithoutPin) {
      sendError(server, 409, "Enable PIN-free physical use before removing the PIN");
      return;
    }
    if (record == nullptr) {
      sendError(server, 409, "Keep a PIN for hardware-independent profile login");
      return;
    }
    if (!TurnHubProfiles::clearPinForProfile(profileId)) {
      sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not remove PIN\"}");
      return;
    }
    TurnHubProfiles::clearPinForSeat(record->mac, session->slot);
    session->pinVerified = false;
  }

  if (server.hasArg("pin") || clearPin) endOtherSessions(*session, profileId);

  // Sigils that render player names need the new name pushed to them.
  SigilBus *bus = SigilBus::activeInstance();
  if (displayProfileChanged && record != nullptr && bus != nullptr && record->helloInfoValid &&
      (record->capabilities & TurnHubProtocol::CAPABILITY_DISPLAY_PROFILE) != 0) {
    bus->syncDisplayProfile(session->controllerId);
  }
  sendJson(server, 200, "{\"ok\":true}");
}

void handleProfilePolicy(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign into your profile to change its settings");
    return;
  }
  const String id = sessionProfileId(*session);
  const String physical = server.arg("allowPhysicalWithoutPin");
  const String stats = server.arg("hideStatsWithoutAuthentication");
  if ((physical != "0" && physical != "1") || (stats != "0" && stats != "1")) {
    sendError(server, 400, "Both profile choices must be 0 or 1");
    return;
  }
  if (physical == "0" && !TurnHubProfiles::hasPinForProfile(id)) {
    sendError(server, 409, "Set a PIN before requiring authentication for physical use");
    return;
  }
  TurnHubProfiles::ProfilePolicy policy;
  policy.allowPhysicalWithoutPin = physical == "1";
  policy.hideStatsWithoutAuthentication = stats == "1";
  if (!TurnHubProfiles::savePolicyForProfile(id, policy)) {
    sendError(server, 503, "Profile settings could not be saved; reload before retrying");
    return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

void handleProfileStats(WebServer &server) {
  String profileId;
  String name;
  ProfileStats stats{};
  const WebSession *session = loadSessionStats(server, profileId, name, stats);
  if (!session) return;

  String response;
  response.reserve(1100);
  response += "{\"ok\":true,\"profileId\":\"";
  response += jsonEscape(profileId);
  response += "\",\"name\":\"";
  response += jsonEscape(name);
  response += "\",\"winRate\":\"";
  response += winRateText(stats);
  response += "\",\"lifetime\":{";
  response += "\"gamesPlayed\":" + String(stats.gamesPlayed);
  response += ",\"gamesWon\":" + String(stats.gamesWon);
  response += ",\"gamesStarted\":" + String(stats.gamesStarted);
  response += ",\"gamesEliminated\":" + String(stats.gamesEliminated);
  response += ",\"turnsCompleted\":" + String(stats.turnsCompleted);
  response += ",\"totalTurnMs\":" + uint64Text(stats.totalTurnMs);
  response += ",\"averageTurnMs\":" + String(averageMs(stats.totalTurnMs, stats.turnsCompleted));
  response += ",\"fastestTurnMs\":" + String(stats.fastestTurnMs);
  response += ",\"longestTurnMs\":" + String(stats.longestTurnMs);
  response += ",\"totalGameMs\":" + uint64Text(stats.totalGameMs);
  response += ",\"averageGameMs\":" + String(averageMs(stats.totalGameMs, stats.gamesPlayed));
  response += "},\"lastGame\":{\"result\":\"";
  response += TurnHubProfileStats::resultName(stats.lastGameResult);
  response += "\",\"durationMs\":" + String(stats.lastGameDurationMs);
  response += ",\"turns\":" + String(stats.lastGameTurns);
  response += ",\"turnMs\":" + String(stats.lastGameTurnMs);
  response += ",\"averageTurnMs\":" + String(averageMs(stats.lastGameTurnMs, stats.lastGameTurns));
  response += ",\"fastestTurnMs\":" + String(stats.lastGameFastestTurnMs);
  response += ",\"longestTurnMs\":" + String(stats.lastGameLongestTurnMs);
  response += "},\"moderation\":" + moderationJson(*session, profileId);
  response += '}';
  sendJson(server, 200, response);
}

void handleProfileStatsExport(WebServer &server) {
  String profileId;
  String name;
  ProfileStats stats{};
  if (!loadSessionStats(server, profileId, name, stats)) return;

  // Exports are meant to be shared, so they never include moderation history.
  const String report = TurnHubProfileStats::buildTextReport(profileId, name, stats);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition",
      String("attachment; filename=\"") + safeExportName(name, profileId) + "\"");
  server.send(200, "text/plain; charset=utf-8", report);
}

}  // namespace internal
}  // namespace TurnHubWebApi
