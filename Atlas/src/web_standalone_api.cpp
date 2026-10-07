// The standalone tablet game's finished games (standalone_import.h): the
// Android app sends each one once it is connected and signed in. Atlas
// imports it once and credits the players that match its profiles.

#include "web_api_internal.h"

#include "game_profile.h"
#include "standalone_import.h"

namespace TurnHubWebApi {
namespace internal {

namespace {

using TurnHubStandalone::ImportedGame;
using TurnHubStandalone::ImportStatus;

// Whole numbers up to max; empty or anything else is invalid.
bool parseCount(const String &text, uint32_t max, uint32_t &value) {
  if (!text.length() || text.length() > 10) return false;
  uint64_t number = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    number = number * 10 + static_cast<uint64_t>(text[i] - '0');
  }
  if (number > max) return false;
  value = static_cast<uint32_t>(number);
  return true;
}

// A player index, or -1 when the field is empty (no starter, no winner).
bool parseIndex(const String &text, uint8_t players, int8_t &value) {
  if (!text.length()) {
    value = -1;
    return true;
  }
  uint32_t index = 0;
  if (!parseCount(text, players - 1u, index)) return false;
  value = static_cast<int8_t>(index);
  return true;
}

// The numbered field of one player, as name0, turns1 and so on.
String playerField(WebServer &server, const char *prefix, uint8_t index) {
  char key[16];
  snprintf(key, sizeof(key), "%s%u", prefix, static_cast<unsigned>(index));
  return server.arg(key);
}

bool parseGame(WebServer &server, ImportedGame &game) {
  constexpr uint32_t DAY_MS = 86400000UL;
  constexpr uint32_t MAX_TURNS = 100000;
  uint32_t players = 0;
  TurnHub::GameProfile profile = TurnHub::GameProfile::Generic;
  game.recordId = server.arg("recordId");
  if (!TurnHubStandalone::validRecordId(game.recordId) ||
      !TurnHub::parseGameProfile(server.arg("gameProfile").c_str(), profile) ||
      !parseCount(server.arg("durationMs"), 7 * DAY_MS, game.durationMs) ||
      !parseCount(server.arg("players"), TurnHubStandalone::MAX_IMPORT_PLAYERS, players) || players < 2) {
    return false;
  }
  game.gameProfile = static_cast<uint8_t>(profile);
  game.playerCount = static_cast<uint8_t>(players);
  if (!parseIndex(server.arg("starter"), game.playerCount, game.starter) ||
      !parseIndex(server.arg("winner"), game.playerCount, game.winner)) {
    return false;
  }
  for (uint8_t i = 0; i < game.playerCount; ++i) {
    auto &player = game.players[i];
    uint32_t out = 0;
    player.name = cleanName(playerField(server, "name", i));
    player.profileId = playerField(server, "profile", i);
    if (player.name.length() == 0 || player.profileId.length() > TurnHubProfiles::PROFILE_ID_LENGTH ||
        !parseCount(playerField(server, "turns", i), MAX_TURNS, player.turnsCompleted) ||
        !parseCount(playerField(server, "turnMs", i), 7 * DAY_MS, player.turnMs) ||
        !parseCount(playerField(server, "fastest", i), DAY_MS, player.fastestTurnMs) ||
        !parseCount(playerField(server, "longest", i), DAY_MS, player.longestTurnMs) ||
        !parseCount(playerField(server, "out", i), game.playerCount, out)) {
      return false;
    }
    player.outOrder = static_cast<uint8_t>(out);
  }
  return true;
}

}  // namespace

void handleStandaloneImport(WebServer &server) {
  if (!sessionForRequest(server)) {
    sendError(server, 401, "Sign in first");
    return;
  }
  ImportedGame game;
  if (!parseGame(server, game)) {
    sendError(server, 400, "That game record is not valid");
    return;
  }
  const auto result = TurnHubStandalone::importGame(game);
  switch (result.status) {
    case ImportStatus::Invalid:
      sendError(server, 400, "That game record is not valid");
      return;
    case ImportStatus::StorageError:
      sendError(server, 503, "Atlas couldn't save that game; try again");
      return;
    default:
      break;
  }
  String body = "{\"ok\":true,\"duplicate\":";
  body += result.status == ImportStatus::Duplicate ? "true" : "false";
  body += ",\"credited\":";
  body += String(result.credited);
  body += ",\"unmatched\":[";
  for (uint8_t i = 0; i < result.unmatchedCount; ++i) {
    if (i) body += ',';
    body += '"';
    body += jsonEscape(result.unmatched[i]);
    body += '"';
  }
  body += "]}";
  sendJson(server, 200, body);
}

}  // namespace internal
}  // namespace TurnHubWebApi
