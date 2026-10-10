// Profile endpoints: the sign-in list, registration, table participation,
// name/PIN edits, the per-profile policy and statistics.

#include "account_access.h"
#include "profile_statistics.h"
#include "web_api_internal.h"
#include "avatars.h"
#include "avatar_artwork.h"
#include "game_profile.h"

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
WebSession *loadSessionStats(WebServer &server, String &profileId, String &name, ProfileStats &stats,
    bool *detailed = nullptr) {
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
  if (!TurnHubProfiles::loadStatsForProfile(profileId, stats, detailed)) {
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
    sendError(server, 400, "A name (1-32 characters) and a 4-8 digit PIN or an 8-64 character password are required");
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
      sendJson(server, 400, "{\"ok\":false,\"error\":\"Use a 4-8 digit PIN or a password of 8 to 64 characters\"}");
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

  // Every Sigil shows player names: push the new one.
  SigilBus *bus = SigilBus::activeInstance();
  if (displayProfileChanged && record != nullptr && bus != nullptr) {
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

namespace {
void sendAccessibility(WebServer &server, const TurnHubProfiles::AccessibilityPrefs &prefs,
    bool stored) {
  String body;
  body.reserve(420);
  body += "{\"ok\":true,\"stored\":";
  body += stored ? "true" : "false";
  body += ",\"sigilSound\":";
  body += prefs.sigilSound ? "true" : "false";
  body += ",\"ledStyle\":\"";
  body += TurnHubProfiles::ledStyleKey(prefs.ledStyle);
  body += "\",\"longPressMs\":";
  body += String(prefs.longPressMs);
  body += ",\"winHoldMs\":";
  body += String(prefs.winHoldMs);
  body += ",\"lifeApprovalMs\":";
  body += String(prefs.lifeApprovalMs);
  body += ",\"lifeApprovalOptionsMs\":[";
  for (uint8_t i = 0; i < TurnHubProfiles::LIFE_APPROVAL_OPTION_COUNT; ++i) {
    if (i) body += ',';
    body += String(TurnHubProfiles::LIFE_APPROVAL_OPTIONS_MS[i]);
  }
  body += "],\"limits\":{\"longPressMinMs\":";
  body += String(TurnHubProtocol::MIN_LONG_PRESS_MS);
  body += ",\"longPressMaxMs\":";
  body += String(TurnHubProtocol::MAX_LONG_PRESS_MS);
  body += ",\"winHoldMinMs\":";
  body += String(TurnHubProtocol::MIN_WIN_HOLD_MS);
  body += ",\"winHoldMaxMs\":";
  body += String(TurnHubProtocol::MAX_WIN_HOLD_MS);
  body += ",\"minGapMs\":";
  body += String(TurnHubProtocol::MIN_HOLD_GAP_MS);
  body += ",\"stepMs\":";
  body += String(TurnHubProtocol::HOLD_STEP_MS);
  body += "}}";
  sendJson(server, 200, body);
}
}  // namespace

// The signed-in profile's personalization: Jewel color ("#rrggbb" or null)
// and avatar (0 none, 1..AVATAR_COUNT a preset; see avatars.h). Both live on
// the microSD card, so without one the response says so and saving fails.
void handlePersonalization(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign into your profile to see its personalization");
    return;
  }
  const String id = sessionProfileId(*session);
  uint32_t rgb = 0;
  const bool set = TurnHubProfiles::jewelColorForProfile(id, rgb);
  char color[8];
  snprintf(color, sizeof(color), "#%06lx", static_cast<unsigned long>(rgb & 0xFFFFFFUL));
  String body = "{\"ok\":true,\"card\":";
  body += TurnHubProfiles::luxuryStoreAvailable() ? "true" : "false";
  body += ",\"color\":";
  if (set) { body += '"'; body += color; body += '"'; } else { body += "null"; }
  body += ",\"avatar\":";
  body += String(TurnHubProfiles::avatarForProfile(id));
  body += ",\"customAvatar\":\"" + jsonEscape(TurnHubProfiles::artworkPath(id)) + "\"";
  body += ",\"pendingAvatar\":\"" + jsonEscape(TurnHubProfiles::artworkPath(id, true)) + "\"";
  body += '}';
  sendJson(server, 200, body);
}

// Omitted fields keep their value. color=#rrggbb or none; avatar=0..count
// Approved custom avatars may also be selected. Pending artwork stays private.
void handleSavePersonalization(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign into your profile to change its personalization");
    return;
  }
  if (!TurnHubProfiles::luxuryStoreAvailable()) {
    sendError(server, 503, "Personalization is saved on the microSD card; insert one first");
    return;
  }
  const String id = sessionProfileId(*session);
  bool colorGiven = server.hasArg("color"), setColor = false;
  uint32_t rgb = 0;
  if (colorGiven) {
    const String value = server.arg("color");
    setColor = value != "none";
    if (setColor) {
      bool valid = value.length() == 7 && value[0] == '#';
      for (size_t i = 1; valid && i < 7; ++i) {
        const char c = value[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
            c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        valid = digit >= 0;
        rgb = (rgb << 4) | static_cast<uint32_t>(digit < 0 ? 0 : digit);
      }
      if (!valid) {
        sendError(server, 400, "color must be #rrggbb or none");
        return;
      }
    }
  }
  int avatar = -1;
  if (server.hasArg("avatar")) {
    const String value = server.arg("avatar");
    bool digits = value.length() > 0 && value.length() <= 3;
    for (size_t i = 0; digits && i < value.length(); ++i) digits = value[i] >= '0' && value[i] <= '9';
    avatar = digits ? value.toInt() : -1;
    if (!(avatar == 0 || (avatar >= 1 && avatar <= TurnHubAvatars::AVATAR_COUNT) ||
          (avatar == TurnHubAvatars::AVATAR_CUSTOM && TurnHubProfiles::artworkPath(id).length() != 0))) {
      sendError(server, 400, "Choose None, a preset or an approved uploaded image");
      return;
    }
  }
  if ((colorGiven && !TurnHubProfiles::saveJewelColorForProfile(id, setColor, rgb)) ||
      (avatar >= 0 && !TurnHubProfiles::saveAvatarForProfile(id, static_cast<uint8_t>(avatar)))) {
    sendError(server, 503, "Personalization could not be saved to the card");
    return;
  }
  handlePersonalization(server);
}

// Public: the preset avatar icons, so the portal and apps draw exactly what
// Atlas and the Sigils draw. Rows are 16 characters, '#' ink, '.' background.
void handleAvatars(WebServer &server) {
  String body;
  body.reserve(6000);
  body += "{\"size\":";
  body += String(TurnHubAvatars::AVATAR_SIZE);
  body += ",\"avatars\":[";
  for (uint8_t i = 0; i < TurnHubAvatars::AVATAR_COUNT; ++i) {
    const auto &icon = TurnHubAvatars::AVATARS[i];
    if (i) body += ',';
    body += "{\"id\":"; body += String(i + 1);
    body += ",\"key\":\""; body += icon.key;
    body += "\",\"label\":\""; body += icon.label;
    body += "\",\"rows\":[";
    for (uint8_t row = 0; row < TurnHubAvatars::AVATAR_SIZE; ++row) {
      if (row) body += ',';
      body += '"'; body += icon.rows[row]; body += '"';
    }
    body += "]}";
  }
  body += "]}";
  sendJson(server, 200, body);
}

// Per-player accessibility preferences. Like the profile policy, these are
// profile settings rather than table state: only the signed-in profile reads
// or changes its own, and the portal and Android use this same endpoint.
void handleAccessibility(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign into your profile to see its accessibility settings");
    return;
  }
  TurnHubProfiles::AccessibilityPrefs prefs;
  const bool stored = TurnHubProfiles::loadAccessibilityForProfile(sessionProfileId(*session), prefs);
  sendAccessibility(server, prefs, stored);
}

// Omitted fields keep their saved value, so a client can change one setting.
void handleSaveAccessibility(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign into your profile to change its accessibility settings");
    return;
  }
  const String id = sessionProfileId(*session);
  TurnHubProfiles::AccessibilityPrefs prefs;
  if (!TurnHubProfiles::loadAccessibilityForProfile(id, prefs)) {
    sendError(server, 503, "Accessibility settings could not be read; reload before retrying");
    return;
  }
  const auto wholeMs = [](const String &text, uint16_t &out) {
    if (text.length() == 0 || text.length() > 5) return false;
    for (size_t i = 0; i < text.length(); ++i) {
      if (text[i] < '0' || text[i] > '9') return false;
    }
    const long value = text.toInt();
    if (value > 65535) return false;
    out = static_cast<uint16_t>(value);
    return true;
  };
  if (server.hasArg("sigilSound")) {
    const String sound = server.arg("sigilSound");
    if (sound != "0" && sound != "1") {
      sendError(server, 400, "sigilSound must be 0 or 1");
      return;
    }
    prefs.sigilSound = sound == "1";
  }
  if (server.hasArg("ledStyle") &&
      !TurnHubProfiles::parseLedStyle(server.arg("ledStyle").c_str(), prefs.ledStyle)) {
    sendError(server, 400, "Unknown light style");
    return;
  }
  uint8_t approvalCode = 0;
  if (server.hasArg("lifeApprovalMs") &&
      (!wholeMs(server.arg("lifeApprovalMs"), prefs.lifeApprovalMs) ||
       !TurnHubProfiles::lifeApprovalCode(prefs.lifeApprovalMs, approvalCode))) {
    sendError(server, 400, "Life approval must be 15, 30 or 60 seconds");
    return;
  }
  if ((server.hasArg("longPressMs") && !wholeMs(server.arg("longPressMs"), prefs.longPressMs)) ||
      (server.hasArg("winHoldMs") && !wholeMs(server.arg("winHoldMs"), prefs.winHoldMs)) ||
      !TurnHubProfiles::validAccessibilityPrefs(prefs)) {
    sendError(server, 400, "Hold times are out of range, or the win hold is not at least one second longer than the long press");
    return;
  }
  if (!TurnHubProfiles::saveAccessibilityForProfile(id, prefs)) {
    sendError(server, 503, "Accessibility settings could not be saved; reload before retrying");
    return;
  }
  if (accessibilityChanged) accessibilityChanged();
  sendAccessibility(server, prefs, true);
}

void handleProfileStats(WebServer &server) {
  String profileId;
  String name;
  ProfileStats stats{};
  bool detailed = false;
  const WebSession *session = loadSessionStats(server, profileId, name, stats, &detailed);
  if (!session) return;

  String response;
  response.reserve(1100);
  response += "{\"ok\":true,\"profileId\":\"";
  response += jsonEscape(profileId);
  response += "\",\"name\":\"";
  response += jsonEscape(name);
  response += "\",\"winRate\":\"";
  response += winRateText(stats);
  // "detailed": false without a microSD card; only games played and won,
  // the last result and the last game type are then kept.
  response += "\",\"detailed\":";
  response += detailed ? "true" : "false";
  response += ",\"lastGameType\":\"";
  response += TurnHub::gameProfileKey(static_cast<TurnHub::GameProfile>(
      stats.lastGameProfile < static_cast<uint8_t>(TurnHub::GameProfile::Count) ? stats.lastGameProfile : 0));
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


namespace {
using TurnHubArtwork::Image;
using TurnHubStorage::Status;
struct ArtworkUpload {
  String profile;
  Image image;
  size_t received = 0;
  uint32_t touched = 0;
} artworkUpload;
String artworkKey(char kind, const String &profile) { String key; key += kind; key += profile; return key; }
void discardUnreferencedArtwork(TurnHubStorage::BlobStore &store, const String &profile, const Image &old) {
  for (char reference : {'a', 'p', 'u'}) {
    Image retained;
    const Status status = TurnHubArtwork::metadata(store, artworkKey(reference, profile).c_str(), retained);
    if (status != Status::NotFound && (status != Status::Ok || retained.revision == old.revision)) return;
  }
  TurnHubArtwork::discard(store, old);
}
bool removeArtwork(TurnHubStorage::BlobStore &store, char kind, const String &profile) {
  Image old;
  const String key = artworkKey(kind, profile);
  const Status read = TurnHubArtwork::metadata(store, key.c_str(), old);
  if (read == Status::NotFound) return true;
  if (read != Status::Ok) return false;
  if (store.remove(key.c_str()) != Status::Ok) return false;
  discardUnreferencedArtwork(store, profile, old);
  return true;
}
bool decimal(const String &s, size_t &value) {
  if (s.length() == 0 || s.length() > 6) return false;
  value = 0;
  for (size_t i = 0; i < s.length(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    value = value * 10 + s[i] - '0';
  }
  return true;
}
String imageRevision(const Image &image) {
  char revision[9]; snprintf(revision, sizeof(revision), "%08lx", static_cast<unsigned long>(image.revision));
  return String(revision);
}
}

void handleAvatarUpload(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session || session->tableDevice) { sendError(server, 401, "Sign into your personal profile first"); return; }
  const String profile = sessionProfileId(*session);
  auto *store = TurnHubProfiles::profileArtworkStore();
  if (!store) { sendError(server, 503, "Insert a working microSD card to save an image"); return; }
  const String action = server.arg("action");
  if (action == "remove") {
    // Switch to a safe fallback before deleting any public artwork.
    if (!TurnHubProfiles::saveAvatarForProfile(profile, 0) ||
        !removeArtwork(*store, 'p', profile) || !removeArtwork(*store, 'a', profile) ||
        !removeArtwork(*store, 'u', profile)) {
      sendError(server, 503, "The image could not be removed from the card"); return;
    }
    if (artworkUpload.profile == profile) artworkUpload = {};
    handlePersonalization(server); return;
  }
  if (action == "start") {
    size_t size = 0;
    if (!decimal(server.arg("size"), size) || size < 32 || size > TurnHubArtwork::MAX_BYTES) {
      sendError(server, 400, "Use a square JPEG up to 512 pixels and 48 KiB"); return;
    }
    if (artworkUpload.profile.length() != 0 && artworkUpload.profile != profile &&
        uint32_t(millis() - artworkUpload.touched) < 120000) {
      sendError(server, 409, "Another image is uploading; try again shortly"); return;
    }
    if (!removeArtwork(*store, 'u', profile)) { sendError(server, 503, "Unfinished image could not be cleared"); return; }
    Image image; image.size = size;
    // Check for an existing chunk before choosing an immutable asset ID.
    for (int attempt = 0; attempt < 8; ++attempt) {
      image.revision = esp_random(); char key[16]; size_t existing = 0;
      TurnHubArtwork::chunkKey(key, image.revision, 0);
      if (image.revision && store->read(key, nullptr, 0, existing) == Status::NotFound) break;
      image.revision = 0;
    }
    if (!image.revision || TurnHubArtwork::publish(*store, artworkKey('u', profile).c_str(), image) != Status::Ok) {
      sendError(server, 503, "Image upload could not start on the card"); return;
    }
    artworkUpload.profile = profile; artworkUpload.image = image; artworkUpload.received = 0; artworkUpload.touched = millis();
    sendJson(server, 200, String("{\"ok\":true,\"revision\":\"") + imageRevision(image) + "\",\"chunkBytes\":768}"); return;
  }
  if (artworkUpload.profile != profile || server.arg("revision") != imageRevision(artworkUpload.image) ||
      uint32_t(millis() - artworkUpload.touched) >= 120000) {
    sendError(server, 409, "Image upload expired; choose the image again"); return;
  }
  // Recheck the persisted staging manifest: a removed/swapped card cannot
  // accept chunks belonging to a transfer started on another card.
  Image stage;
  if (TurnHubArtwork::metadata(*store, artworkKey('u', profile).c_str(), stage) != Status::Ok ||
      stage.revision != artworkUpload.image.revision || stage.size != artworkUpload.image.size) {
    sendError(server, 409, "The image's card changed; start the upload again"); return;
  }
  if (action == "chunk") {
    size_t index = 0;
    const String hex = server.arg("data");
    size_t bytes = artworkUpload.image.size - artworkUpload.received;
    if (bytes > TurnHubArtwork::CHUNK) bytes = TurnHubArtwork::CHUNK;
    if (!bytes || !decimal(server.arg("index"), index) || index != artworkUpload.received / TurnHubArtwork::CHUNK || hex.length() != bytes * 2) {
      sendError(server, 400, "Image chunks must arrive in order with the declared length"); return;
    }
    uint8_t buffer[TurnHubArtwork::CHUNK];
    for (size_t i = 0; i < hex.length(); ++i) {
      const char c = hex[i]; int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
      if (digit < 0) { sendError(server, 400, "Invalid image chunk"); return; }
      if (!(i & 1)) buffer[i / 2] = digit << 4; else buffer[i / 2] |= digit;
    }
    char key[16]; TurnHubArtwork::chunkKey(key, stage.revision, index);
    if (store->write(key, buffer, bytes) != Status::Ok) { sendError(server, 503, "Image chunk could not be saved"); return; }
    artworkUpload.received += bytes; artworkUpload.touched = millis();
    sendJson(server, 200, "{\"ok\":true}"); return;
  }
  if (action != "submit" || artworkUpload.received != stage.size || !TurnHubArtwork::validateJpeg(*store, stage)) {
    sendError(server, 400, "The upload must be a complete square baseline JPEG, 32–512 pixels"); return;
  }
  const String thumbnail = server.arg("thumbnail");
  if (thumbnail.length() != 512) { sendError(server, 400, "Include a 16-pixel RGB332 Atlas thumbnail"); return; }
  uint8_t thumb[257] = {1};
  for (size_t i = 0; i < thumbnail.length(); ++i) {
    char c = thumbnail[i]; int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0) { sendError(server, 400, "Invalid Atlas thumbnail"); return; }
    if (!(i & 1)) thumb[1 + i / 2] = digit << 4; else thumb[1 + i / 2] |= digit;
  }
  char thumbKey[16]; TurnHubArtwork::thumbnailKey(thumbKey, stage.revision);
  if (store->write(thumbKey, thumb, sizeof(thumb)) != Status::Ok) { sendError(server, 503, "Atlas thumbnail could not be saved"); return; }
  Image previous;
  const String pendingKey = artworkKey('p', profile);
  const Status previousStatus = TurnHubArtwork::metadata(*store, pendingKey.c_str(), previous);
  if ((previousStatus != Status::Ok && previousStatus != Status::NotFound) ||
      TurnHubArtwork::publish(*store, pendingKey.c_str(), stage) != Status::Ok) {
    sendError(server, 503, "The image could not be submitted for review"); return;
  }
  // Manifest publication happens first; uncertain failures never discard new chunks.
  store->remove(artworkKey('u', profile).c_str());
  if (previousStatus == Status::Ok) discardUnreferencedArtwork(*store, profile, previous);
  artworkUpload = {};
  handlePersonalization(server);
}

void handleAvatarReview(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const String profile = server.arg("profileId");
  auto *store = TurnHubProfiles::profileArtworkStore();
  if (!store || !TurnHubProfiles::profileExists(profile)) { sendError(server, 503, "Profile or image card unavailable"); return; }
  Image pending;
  if (TurnHubArtwork::metadata(*store, artworkKey('p', profile).c_str(), pending) != Status::Ok ||
      server.arg("revision") != imageRevision(pending)) { sendError(server, 409, "The image awaiting review changed; refresh first"); return; }
  const String action = server.arg("action");
  if (action == "reject") {
    if (!removeArtwork(*store, 'p', profile)) { sendError(server, 503, "Could not reject the image"); return; }
  } else if (action == "approve") {
    if (!TurnHubArtwork::validateJpeg(*store, pending)) { sendError(server, 400, "The saved image is incomplete or invalid"); return; }
    Image old; const String key = artworkKey('a', profile);
    const Status previous = TurnHubArtwork::metadata(*store, key.c_str(), old);
    if ((previous != Status::Ok && previous != Status::NotFound) ||
        TurnHubArtwork::publish(*store, key.c_str(), pending) != Status::Ok ||
        !TurnHubProfiles::saveAvatarForProfile(profile, TurnHubAvatars::AVATAR_CUSTOM)) {
      sendError(server, 503, "Could not approve the image on the card"); return;
    }
    store->remove(artworkKey('p', profile).c_str());
    if (previous == Status::Ok && old.revision != pending.revision) discardUnreferencedArtwork(*store, profile, old);
  } else { sendError(server, 400, "Choose approve or reject"); return; }
  sendJson(server, 200, "{\"ok\":true}");
}

void handleAvatarImage(WebServer &server) {
  const String profile = server.arg("profileId");
  const bool pending = server.arg("pending") == "1";
  if (pending) {
    WebSession *session = sessionForRequest(server);
    TurnHubAccounts::Account account;
    if (!session || (sessionProfileId(*session) != profile &&
        (!TurnHubAccounts::load(sessionProfileId(*session), account) || !(account.permissions & TurnHubAccounts::Admin)))) {
      sendError(server, 403, "Pending images are private to their player and Admins"); return;
    }
  }
  auto *store = TurnHubProfiles::profileArtworkStore();
  Image image;
  if (!store || !TurnHubProfiles::profileExists(profile) ||
      TurnHubArtwork::metadata(*store, artworkKey(pending ? 'p' : 'a', profile).c_str(), image) != Status::Ok ||
      server.arg("revision") != imageRevision(image) || !TurnHubArtwork::validateJpeg(*store, image)) {
    sendError(server, 404, "Image unavailable; use the default icon"); return;
  }
  server.sendHeader("Cache-Control", "private, no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.setContentLength(image.size);
  server.send(200, "image/jpeg", "");
  uint8_t bytes[TurnHubArtwork::CHUNK];
  for (size_t index = 0; index < (image.size + TurnHubArtwork::CHUNK - 1) / TurnHubArtwork::CHUNK; ++index) {
    size_t size = 0;
    if (TurnHubArtwork::readChunk(*store, image, index, bytes, size) != Status::Ok) { server.client().stop(); return; }
    server.sendContent(reinterpret_cast<const char *>(bytes), size);
  }
}

}  // namespace internal
}  // namespace TurnHubWebApi
