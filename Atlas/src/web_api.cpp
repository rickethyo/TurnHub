#include "web_api.h"

#include "optional_preferences.h"
#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "firmware_version.h"
#include "profile_statistics.h"
#include "profile_store.h"
#include "sigil_bus.h"
#include "stats_page.h"
#include "turnhub_types.h"
#include "controller_profiles.h"
#include "login_limiter.h"
#include "profile_login_page.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubWebApi {
namespace {

using TurnHub::INVALID_ID;
using TurnHub::MAX_PHYSICAL_SIGILS;
using TurnHub::MAX_PLAYERS;
using TurnHub::SigilBus;
using TurnHub::SigilRecord;
using TurnHubProfiles::ProfileStats;

constexpr uint32_t CLAIM_TIMEOUT_MS = 30000;
constexpr uint32_t SESSION_TIMEOUT_MS = 8UL * 60UL * 60UL * 1000UL;
constexpr uint8_t MAX_PENDING_CLAIMS = 6;
constexpr uint8_t MAX_WEB_SESSIONS = MAX_PLAYERS * 2;
constexpr char WIFI_PREF_NAMESPACE[] = "atlas-net";
constexpr char WIFI_PREF_KEY[] = "ap-pass";

struct PendingClaim {
  bool used = false;
  uint64_t requestId = 0;
  uint8_t controllerId = INVALID_ID;
  uint8_t slot = 1;
  uint32_t createdMs = 0;
  bool approved = false;
  char token[33] = {};
  char requestingToken[33] = {};
  char profileId[TurnHubProfiles::PROFILE_ID_LENGTH + 1] = {};
  char error[100] = {};
};

struct WebSession {
  bool used = false;
  uint8_t controllerId = INVALID_ID;
  uint8_t slot = 1;
  char token[33] = {};
  char profileId[TurnHubProfiles::PROFILE_ID_LENGTH + 1] = {};
  uint32_t lastSeenMs = 0;
};

PendingClaim pendingClaims[MAX_PENDING_CLAIMS];
WebSession sessions[MAX_WEB_SESSIONS];
WebServer *webServer = nullptr;
ResolveSeatCallback resolveSeat = nullptr;
ControlCallback controlHandler = nullptr;
ProfileControlCallback profileControlHandler = nullptr;
ResolveProfileCallback resolveProfile = nullptr;
GameSettingsCallback readGameConfiguration = nullptr;
ConfigureGameCallback configureGameHandler = nullptr;
ChangeLifeCallback changeLifeHandler = nullptr;
ReadCountersCallback readCountersHandler = nullptr;
CounterControlCallback counterControlHandler = nullptr;
TurnHub::LoginLimiter loginLimiter;
bool profileStoreReady = false;
ModerateCallback moderateHandler = nullptr;
StateCallback readClientState = nullptr;
RevisionCallback readClientRevision = nullptr;
char bootId[33] = {};

uint64_t random64() {
  return (static_cast<uint64_t>(esp_random()) << 32) |
      static_cast<uint64_t>(esp_random());
}

void makeToken(char out[33]) {
  const uint32_t a = esp_random();
  const uint32_t b = esp_random();
  const uint32_t c = esp_random();
  const uint32_t d = esp_random();
  snprintf(
      out,
      33,
      "%08lX%08lX%08lX%08lX",
      static_cast<unsigned long>(a),
      static_cast<unsigned long>(b),
      static_cast<unsigned long>(c),
      static_cast<unsigned long>(d));
}

String requestIdText(uint64_t value) {
  char text[17];
  snprintf(
      text,
      sizeof(text),
      "%08lX%08lX",
      static_cast<unsigned long>(value >> 32),
      static_cast<unsigned long>(value & 0xFFFFFFFFULL));
  return String(text);
}

String uint64Text(uint64_t value) {
  char text[32];
  snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
  return String(text);
}

uint64_t parseRequestId(const String &value) {
  if (value.length() != 16) {
    return 0;
  }
  char *end = nullptr;
  const uint64_t parsed = strtoull(value.c_str(), &end, 16);
  return end != nullptr && *end == '\0' ? parsed : 0;
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) >= 0x20) {
          out += c;
        }
        break;
    }
  }
  return out;
}

void sendJson(WebServer &server, int status, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json", body);
}

bool masterButtonPressed() {
  return digitalRead(AtlasConfig::MASTER_BUTTON_PIN) == LOW;
}

bool requireMasterButton(WebServer &server) {
  if (masterButtonPressed()) {
    return true;
  }
  sendJson(
      server,
      403,
      "{\"ok\":false,\"error\":\"Hold the physical Atlas master button while saving this system setting\"}");
  return false;
}

bool validSlot(int slot) {
  return slot == 1 || slot == 2;
}

bool resolveSeatNow(uint8_t controllerId, uint8_t slot, SeatSnapshot &snapshot) {
  snapshot = SeatSnapshot{};
  if (resolveSeat == nullptr || !validSlot(slot)) {
    return false;
  }
  return resolveSeat(controllerId, slot, snapshot) && snapshot.exists;
}

const SigilRecord *recordForModule(uint8_t controllerId) {
  SigilBus *bus = SigilBus::activeInstance();
  return bus != nullptr ? bus->record(controllerId) : nullptr;
}

String profileIdForPhysicalSeat(uint8_t controllerId, uint8_t slot) {
  return TurnHubControllers::profileForSeat(controllerId, slot);
}

String macText(const uint8_t mac[6]) {
  char text[18];
  snprintf(
      text,
      sizeof(text),
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

String sigilHardwareId(const uint8_t mac[6]) {
  char text[17];
  snprintf(
      text,
      sizeof(text),
      "THS-%02X%02X%02X%02X%02X%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

String atlasHardwareId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  return String("THA-") + mac;
}

String customDeviceName(uint8_t controllerId) {
  const SigilRecord *record = recordForModule(controllerId);
  return record != nullptr ? TurnHubProfiles::deviceName(record->mac) : String();
}

String deviceLabel(uint8_t controllerId) {
  String label = customDeviceName(controllerId);
  if (label.length() == 0) {
    label = "Sigil ";
    label += String(controllerId + 1);
  }
  return label;
}

bool hasPin(uint8_t controllerId, uint8_t slot) {
  const String profileId = profileIdForPhysicalSeat(controllerId, slot);
  if (TurnHubProfiles::hasPinForProfile(profileId)) {
    return true;
  }
  const SigilRecord *record = recordForModule(controllerId);
  return record != nullptr && TurnHubProfiles::hasPinForSeat(record->mac, slot);
}

bool validPin(const String &pin) {
  if (pin.length() < 4 || pin.length() > 8) {
    return false;
  }
  for (size_t i = 0; i < pin.length(); ++i) {
    if (pin[i] < '0' || pin[i] > '9') {
      return false;
    }
  }
  return true;
}

String sha256Hex(const String &material) {
  uint8_t digest[32] = {};
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts_ret(&context, 0) != 0 ||
      mbedtls_sha256_update_ret(
          &context,
          reinterpret_cast<const unsigned char *>(material.c_str()),
          material.length()) != 0 ||
      mbedtls_sha256_finish_ret(&context, digest) != 0) {
    mbedtls_sha256_free(&context);
    return String();
  }
  mbedtls_sha256_free(&context);

  char hex[65];
  for (uint8_t i = 0; i < 32; ++i) {
    snprintf(hex + (i * 2), 3, "%02x", digest[i]);
  }
  hex[64] = '\0';
  return String(hex);
}

String profilePinHash(const String &profileId, const String &pin) {
  TurnHubIdentity::ProfileId parsed;
  if (!TurnHubIdentity::ProfileId::parse(profileId.c_str(), parsed)) {
    return String();
  }
  String material = "TurnHubProfile:";
  material += profileId;
  material += ':';
  material += pin;
  return sha256Hex(material);
}

String legacyPinHash(
    const uint8_t mac[6],
    uint8_t slot,
    const String &pin) {
  String material = sigilHardwareId(mac);
  material += ':';
  material += String(slot);
  material += ':';
  material += pin;
  return sha256Hex(material);
}

bool pinMatches(uint8_t controllerId, uint8_t slot, const String &pin) {
  const SigilRecord *record = recordForModule(controllerId);
  if (record == nullptr) {
    return false;
  }

  const String profileId = TurnHubProfiles::profileIdForSeat(record->mac, slot);
  if (!TurnHubProfiles::profileExists(profileId)) {
    return false;
  }

  bool legacySource = false;
  const String stored =
      TurnHubProfiles::storedPinHashForSeat(record->mac, slot, &legacySource);
  if (stored.length() != 64) {
    return false;
  }

  const String currentCandidate = profilePinHash(profileId, pin);
  if (currentCandidate.length() == 64 && stored.equalsIgnoreCase(currentCandidate)) {
    return true;
  }

  // Existing firmware hashed PINs against THS-MAC + seat. Accept that once,
  // then rewrite it against the durable profile ID so the PIN can follow the
  // player to another physical or future virtual seat.
  const String legacyCandidate = legacyPinHash(record->mac, slot, pin);
  if (legacyCandidate.length() != 64 || !stored.equalsIgnoreCase(legacyCandidate)) {
    return false;
  }

  if (currentCandidate.length() == 64) {
    TurnHubProfiles::setPinHashForSeat(record->mac, slot, currentCandidate);
  }
  return true;
}

void cleanup(uint32_t nowMs) {
  for (auto &pending : pendingClaims) {
    if (pending.used &&
        nowMs - pending.createdMs > CLAIM_TIMEOUT_MS) {
      pending = PendingClaim{};
    }
  }

  for (auto &session : sessions) {
    if (session.used && nowMs - session.lastSeenMs > SESSION_TIMEOUT_MS) {
      session = WebSession{};
    }
  }
}

WebSession *sessionForToken(const String &token, uint32_t nowMs) {
  cleanup(nowMs);
  if (token.length() != 32) {
    return nullptr;
  }

  for (auto &session : sessions) {
    if (session.used && token.equalsIgnoreCase(session.token)) {
      TurnHubAccounts::Account account;
      if(!TurnHubAccounts::load(String(session.profileId),account)||account.archived){session=WebSession{};return nullptr;}
      session.lastSeenMs = nowMs;
      return &session;
    }
  }
  return nullptr;
}

WebSession *sessionForRequest(WebServer &server) {
  return sessionForToken(server.header("X-TurnHub-Token"), millis());
}

WebSession *createProfileSession(const String &profileId, uint32_t nowMs) {
  cleanup(nowMs);
  TurnHubAccounts::Account account;
  if(!TurnHubAccounts::load(profileId,account)||account.archived)return nullptr;
  if (!TurnHubProfiles::profileExists(profileId)) {
    return nullptr;
  }

  WebSession *target = nullptr;

  if (target == nullptr) {
    for (auto &session : sessions) {
      if (!session.used) {
        target = &session;
        break;
      }
    }
  }

  if (target == nullptr) {
    return nullptr;
  }

  *target = WebSession{};
  target->used = true;
  target->lastSeenMs = nowMs;
  makeToken(target->token);
  strncpy(
      target->profileId,
      profileId.c_str(),
      sizeof(target->profileId) - 1);
  target->profileId[sizeof(target->profileId) - 1] = '\0';
  return target;
}

WebSession *createSession(uint8_t controllerId, uint8_t slot, uint32_t nowMs) {
  auto *session = createProfileSession(profileIdForPhysicalSeat(controllerId, slot), nowMs);
  if (session) { session->controllerId = controllerId; session->slot = slot; }
  return session;
}

bool resolveSessionParticipant(WebSession &session) {
  session.controllerId = INVALID_ID;
  session.slot = 1;
  return resolveProfile && resolveProfile(String(session.profileId), session.controllerId, session.slot);
}

String sessionProfileId(const WebSession &session) {
  const String profileId(session.profileId);
  return TurnHubProfiles::profileExists(profileId) ? profileId : String();
}

bool seatHasSession(uint8_t controllerId, uint8_t slot, uint32_t nowMs) {
  cleanup(nowMs);
  for (auto &session : sessions) {
    if (session.used) resolveSessionParticipant(session);
    if (session.used && session.controllerId == controllerId && session.slot == slot) {
      return true;
    }
  }
  return false;
}

uint8_t moduleSessionCount(uint8_t controllerId, uint32_t nowMs) {
  cleanup(nowMs);
  uint8_t count = 0;
  for (auto &session : sessions) {
    if (session.used) resolveSessionParticipant(session);
    if (session.used && session.controllerId == controllerId) {
      ++count;
    }
  }
  return count;
}

uint32_t averageMs(uint64_t total, uint32_t count) {
  return count == 0 ? 0 : static_cast<uint32_t>(total / count);
}

String winRateText(const ProfileStats &stats) {
  const uint32_t tenths = stats.gamesPlayed == 0
      ? 0
      : static_cast<uint32_t>(
          (static_cast<uint64_t>(stats.gamesWon) * 1000ULL) /
          stats.gamesPlayed);
  char text[16];
  snprintf(
      text,
      sizeof(text),
      "%lu.%lu%%",
      static_cast<unsigned long>(tenths / 10),
      static_cast<unsigned long>(tenths % 10));
  return String(text);
}

String safeExportName(const String &name, const String &profileId) {
  String safe;
  safe.reserve(40);
  for (size_t i = 0; i < name.length() && safe.length() < 24; ++i) {
    const char c = name[i];
    if ((c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
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

bool loadSessionStats(
    WebServer &server,
    WebSession *&session,
    String &profileId,
    String &name,
    ProfileStats &stats) {
  session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return false;
  }

  profileId = sessionProfileId(*session);
  if (profileId.length() == 0) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"This session is not attached to a durable profile\"}");
    return false;
  }

  name = TurnHubProfiles::nameForProfile(profileId);
  if (!TurnHubProfiles::loadStatsForProfile(profileId, stats)) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Profile statistics storage unavailable\"}");
    return false;
  }
  return true;
}

void handleDevices(WebServer &server) {
  SigilBus *bus = SigilBus::activeInstance();
  const uint32_t nowMs = millis();

  String json;
  json.reserve(3800);
  json += "{\"atlas\":{\"hardwareId\":\"";
  json += atlasHardwareId();
  json += "\",\"firmware\":\"";
  json += TurnHubFirmware::VERSION;
  json += "\"},\"devices\":[";

  bool first = true;
  if (bus != nullptr) {
    for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
      const SigilRecord *record = bus->record(id);
      if (record == nullptr) {
        continue;
      }

      if (!first) {
        json += ',';
      }
      first = false;

      const uint32_t ageMs = nowMs - record->lastSeenMs;
      const bool online = ageMs <= SigilBus::SIGIL_TIMEOUT_MS;
      const String customName = customDeviceName(id);
      const String label = deviceLabel(id);
      const String profileA = TurnHubProfiles::boundProfileIdForSeat(record->mac, 1);
      const bool persistentA = profileA.length() > 0 &&
          TurnHubProfiles::seatIsPersistent(record->mac, 1);

      char firmware[24];
      if (record->helloInfoValid) {
        snprintf(
            firmware,
            sizeof(firmware),
            "%u.%u.%u",
            static_cast<unsigned>(record->firmwareMajor),
            static_cast<unsigned>(record->firmwareMinor),
            static_cast<unsigned>(record->firmwarePatch));
      } else {
        snprintf(firmware, sizeof(firmware), "unknown");
      }

      json += "{\"id\":";
      json += String(id);
      json += ",\"label\":\"";
      json += jsonEscape(label);
      json += "\",\"defaultLabel\":\"Sigil ";
      json += String(id + 1);
      json += "\",\"customName\":\"";
      json += jsonEscape(customName);
      json += "\",\"hardwareId\":\"";
      json += sigilHardwareId(record->mac);
      json += "\",\"mac\":\"";
      json += macText(record->mac);
      json += "\",\"online\":";
      json += online ? "true" : "false";
      json += ",\"ageMs\":";
      json += String(ageMs);
      json += ",\"firmware\":\"";
      json += firmware;
      json += "\",\"metadata\":";
      json += record->helloInfoValid ? "true" : "false";
      json += ",\"capabilities\":";
      json += String(record->capabilities);
      json += ",\"sessionCount\":";
      json += String(moduleSessionCount(id, nowMs));
      json += ",\"profileA\":\"";
      json += jsonEscape(profileA);
      json += "\",\"persistentA\":";
      json += persistentA ? "true" : "false";
      json += '}';
    }
  }

  json += "]}";
  sendJson(server, 200, json);
}

void handleSeatPersistence(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendJson(server, 401, "{\"error\":\"Sign in before changing Sigil persistence\"}");
    return;
  }
  const int module = server.arg("module").toInt();
  const int slot = server.arg("slot").toInt();
  const String remember = server.arg("remember");
  if (module < 0 || module >= MAX_PHYSICAL_SIGILS || slot != 1 ||
      (remember != "0" && remember != "1")) {
    sendJson(server, 400, "{\"error\":\"Only Seat A can have a persistence choice\"}");
    return;
  }
  const SigilRecord *record = recordForModule(static_cast<uint8_t>(module));
  if (!record) {
    sendJson(server, 404, "{\"error\":\"Sigil is not known to Atlas\"}");
    return;
  }
  const String bound = TurnHubProfiles::boundProfileIdForSeat(record->mac, 1);
  if (bound.length() == 0 || bound != sessionProfileId(*session)) {
    sendJson(server, 403, "{\"error\":\"This Sigil seat is not attached to your profile\"}");
    return;
  }
  if (remember == "1") {
    sendJson(server, 409, "{\"error\":\"Sigil seats are temporary; profiles are saved on Atlas\"}");
    return;
  }
  if (!TurnHubProfiles::setSeatPersistent(record->mac, 1, remember == "1")) {
    sendJson(server, 503, "{\"error\":\"Could not save the Seat A persistence choice\"}");
    return;
  }
  sendJson(server, 200, String("{\"ok\":true,\"persistent\":") +
      (remember == "1" ? "true}" : "false}"));
}

void handleDeviceName(WebServer &server) {
  if(!requirePermission(server,TurnHubAccounts::Admin))return;
  if (!requireMasterButton(server)) {
    return;
  }
  if (!profileStoreReady || !server.hasArg("module") || !server.hasArg("name")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Module and name are required\"}");
    return;
  }

  const int module = server.arg("module").toInt();
  const SigilRecord *record =
      module >= 0 && module < MAX_PHYSICAL_SIGILS
      ? recordForModule(static_cast<uint8_t>(module))
      : nullptr;
  if (record == nullptr) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Sigil is not known to Atlas\"}");
    return;
  }

  String name = server.arg("name");
  name.trim();
  if (name.length() > 32) {
    name.remove(32);
  }

  if (!TurnHubProfiles::setDeviceName(record->mac, name)) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save Sigil name\"}");
    return;
  }

  const String label = name.length() == 0
      ? String("Sigil ") + String(module + 1)
      : name;

  serialLog.print("ATLAS|SIGIL|NAME|");
  serialLog.print(module);
  serialLog.print("|");
  serialLog.println(label);

  sendJson(
      server,
      200,
      String("{\"ok\":true,\"label\":\"") + jsonEscape(label) + "\"}");
}

void handleNetworkInfo(WebServer &server) {
  if(!requirePermission(server,TurnHubAccounts::Admin))return;
  TurnHub::OptionalPreferences networkPrefs;
  String password;
  if (networkPrefs.begin(WIFI_PREF_NAMESPACE, true)) {
    password = networkPrefs.getString(WIFI_PREF_KEY, "");
    networkPrefs.end();
  }

  // No owner-set password means Atlas is running on the shipped default.
  const bool ownerSet = password.length() >= 8;
  String response = "{\"ssid\":\"";
  response += jsonEscape(String(AtlasConfig::WIFI_SSID));
  response += "\",\"security\":\"WPA2-PSK\",\"passwordConfigured\":";
  response += ownerSet ? "true" : "false";
  response += ",\"passwordIsDefault\":";
  response += ownerSet ? "false" : "true";
  response += ",\"passwordLength\":";
  response += String(ownerSet ? password.length() : sizeof(AtlasConfig::WIFI_DEFAULT_PASSWORD) - 1);
  response += ",\"stations\":";
  response += String(WiFi.softAPgetStationNum());
  response += ",\"masterButton\":";
  response += masterButtonPressed() ? "true" : "false";
  response += '}';
  sendJson(server, 200, response);
}

void handleNetworkPassword(WebServer &server) {
  if(!requirePermission(server,TurnHubAccounts::Admin))return;
  if (!requireMasterButton(server)) {
    return;
  }
  if (!server.hasArg("password")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"New password is required\"}");
    return;
  }

  const String password = server.arg("password");
  if (password.length() < 8 || password.length() > 63) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Wi-Fi password must be 8 to 63 characters\"}");
    return;
  }

  TurnHub::OptionalPreferences networkPrefs;
  if (!networkPrefs.begin(WIFI_PREF_NAMESPACE, false)) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Network settings storage unavailable\"}");
    return;
  }

  const String existing = networkPrefs.getString(WIFI_PREF_KEY, "");
  if (existing == password) {
    networkPrefs.end();
    sendJson(server, 200, "{\"ok\":true,\"changed\":false,\"message\":\"Wi-Fi password is already set to that value\"}");
    return;
  }

  const size_t written = networkPrefs.putString(WIFI_PREF_KEY, password);
  networkPrefs.end();
  if (written == 0) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save Wi-Fi password\"}");
    return;
  }

  serialLog.println("ATLAS|WIFI_AP|PASSWORD_STORE|UPDATED_FROM_PORTAL");
  sendJson(
      server,
      200,
      "{\"ok\":true,\"changed\":true,\"restarting\":true,\"message\":\"Password saved. Atlas is restarting.\"}");

  delay(450);
  ESP.restart();
}

void handleProfiles(WebServer &server) {
  if (!profileStoreReady) { sendJson(server, 503, "{\"error\":\"Profile storage unavailable\"}"); return; }
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][TurnHubProfiles::PROFILE_ID_LENGTH + 1];
  const size_t count = TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES);
  String json = "{\"profiles\":[";
  bool comma=false;
  for (size_t i = 0; i < count; ++i) {
    TurnHubAccounts::Account account;
    if(!TurnHubAccounts::load(String(ids[i]),account)||account.archived)continue;
    if (comma) json += ',';
    comma=true;
    json += "{\"profileId\":\""; json += ids[i];
    json += "\",\"name\":\""; json += jsonEscape(TurnHubProfiles::nameForProfile(ids[i]));
    json += "\",\"hasPin\":"; json += TurnHubProfiles::hasPinForProfile(ids[i]) ? "true" : "false";
    json += '}';
  }
  json += "]}";
  sendJson(server, 200, json);
}

void sendLogin(WebServer &server, WebSession *session) {
  if (!session) { sendJson(server, 503, "{\"error\":\"No session slots available\"}"); return; }
  sendJson(server, 200, String("{\"ok\":true,\"token\":\"") + session->token +
      "\",\"profileId\":\"" + session->profileId + "\"}");
}

void handleRegistration(WebServer &server) {
  String name = server.arg("name"); name.trim();
  const String pin = server.arg("pin");
  if (name.length() == 0 || name.length() > 32 || !validPin(pin)) {
    sendJson(server, 400, "{\"error\":\"A name (1-32 characters) and a 4-8 digit PIN are required\"}"); return;
  }
  cleanup(millis());
  bool available = false;
  for (const auto &session : sessions) if (!session.used) available = true;
  if (!available) { sendJson(server, 503, "{\"error\":\"No session slots available\"}"); return; }
  const String id = TurnHubProfiles::createProfileWithCredentials(name, pin, profilePinHash);
  if (id.length() == 0) { sendJson(server, 503, "{\"error\":\"Could not create profile; storage may be full\"}"); return; }
  sendLogin(server, createProfileSession(id, millis()));
}

void handleParticipation(WebServer &server, WebControl control) {
  WebSession *session = sessionForRequest(server);
  if (!session) { sendJson(server, 401, "{\"error\":\"Sign in to a profile first\"}"); return; }
  String message;
  if (!profileControlHandler || !profileControlHandler(sessionProfileId(*session), control, INVALID_ID, 1, message)) {
    sendJson(server, 409, String("{\"error\":\"") + jsonEscape(message) + "\"}"); return;
  }
  sendJson(server, 200, String("{\"ok\":true,\"message\":\"") + jsonEscape(message) + "\"}");
}

void handleSeats(WebServer &server) {
  const uint32_t nowMs = millis();
  String json = "{\"seats\":[";
  json.reserve(4200);
  bool first = true;

  for (uint8_t controllerId = 0; controllerId < TurnHub::MAX_CONTROLLERS; ++controllerId) {
    for (uint8_t slot = 1; slot <= 2; ++slot) {
      SeatSnapshot snapshot;
      if (!resolveSeatNow(controllerId, slot, snapshot)) {
        continue;
      }

      if (!first) {
        json += ',';
      }
      first = false;

      const String profileId = profileIdForPhysicalSeat(controllerId, slot);
      const String savedName = TurnHubProfiles::nameForProfile(profileId);
      json += "{\"module\":";
      json += String(controllerId);
      json += ",\"virtual\":";
      json += controllerId >= MAX_PHYSICAL_SIGILS ? "true" : "false";
      json += ",\"slot\":";
      json += String(slot);
      json += ",\"slotName\":\"";
      json += slot == 1 ? "A" : "B";
      json += "\",\"player\":";
      json += String(snapshot.playerNumber);
      json += ",\"active\":";
      json += snapshot.active ? "true" : "false";
      json += ",\"eliminated\":";
      json += snapshot.eliminated ? "true" : "false";
      json += ",\"lifeAvailable\":"; json += snapshot.lifeAvailable ? "true" : "false";
      json += ",\"life\":"; json += String(snapshot.life);
      json += ",\"profileId\":\"";
      json += jsonEscape(profileId);
      json += "\",\"name\":\"";
      json += jsonEscape(savedName);
      json += "\",\"hasPin\":";
      json += hasPin(controllerId, slot) ? "true" : "false";
      json += ",\"sessionClaimed\":";
      json += seatHasSession(controllerId, slot, nowMs) ? "true" : "false";
      json += '}';
    }
  }

  json += "]}";
  sendJson(server, 200, json);
}

void handleSessionRequest(WebServer &server) {
  SigilBus *bus = SigilBus::activeInstance();
  if (bus == nullptr || !server.hasArg("module") || !server.hasArg("slot")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Missing seat\"}");
    return;
  }

  const int module = server.arg("module").toInt();
  const int slot = server.arg("slot").toInt();
  WebSession *requester = sessionForRequest(server);
  SeatSnapshot snapshot;
  if (module < 0 || module >= MAX_PHYSICAL_SIGILS || !validSlot(slot) ||
      !bus->isOnline(static_cast<uint8_t>(module), millis()) ||
      (!requester && !resolveSeatNow(static_cast<uint8_t>(module), static_cast<uint8_t>(slot), snapshot))) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Seat is not available\"}");
    return;
  }

  if (!requester && profileIdForPhysicalSeat(
          static_cast<uint8_t>(module),
          static_cast<uint8_t>(slot)).length() == 0) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"This seat is a guest. Create or sign into an account, then attach the Sigil from the lobby\"}");
    return;
  }

  // PIN-free physical play is not permission to obtain a browser credential.
  // Profiles without a PIN retain the physical bootstrap path to set one.
  if (!requester && hasPin(static_cast<uint8_t>(module), static_cast<uint8_t>(slot))) {
    sendJson(server, 403, "{\"error\":\"Sign in with this profile's PIN to use it in a browser\"}");
    return;
  }

  const uint32_t nowMs = millis();
  cleanup(nowMs);

  for (auto &pending : pendingClaims) {
    if (pending.used &&
        pending.controllerId == static_cast<uint8_t>(module) &&
        pending.slot == static_cast<uint8_t>(slot)) {
      pending = PendingClaim{};
    }
  }

  PendingClaim *target = nullptr;
  for (auto &pending : pendingClaims) {
    if (!pending.used) {
      target = &pending;
      break;
    }
  }

  if (target == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Too many pending claims\"}");
    return;
  }

  uint64_t requestId = random64();
  if (requestId == 0) {
    requestId = 1;
  }

  *target = PendingClaim{};
  target->used = true;
  target->requestId = requestId;
  target->controllerId = static_cast<uint8_t>(module);
  target->slot = static_cast<uint8_t>(slot);
  target->createdMs = nowMs;
  const String claimedProfile = profileIdForPhysicalSeat(target->controllerId, target->slot);
  strncpy(target->profileId, claimedProfile.c_str(), sizeof(target->profileId) - 1);
  if (requester) strncpy(target->requestingToken, requester->token, sizeof(target->requestingToken) - 1);

  String response = "{\"ok\":true,\"status\":\"pending\",\"requestId\":\"";
  response += requestIdText(requestId);
  response += "\",\"module\":";
  response += String(module);
  response += ",\"slot\":";
  response += String(slot);
  response += ",\"expiresMs\":";
  response += String(CLAIM_TIMEOUT_MS);
  response += ",\"message\":\"Press Action on this Sigil to authorize this seat.\"}";
  sendJson(server, 202, response);
}

void handleSessionPoll(WebServer &server) {
  if (!server.hasArg("id")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Missing request id\"}");
    return;
  }

  const uint64_t id = parseRequestId(server.arg("id"));
  if (id == 0) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Invalid request id\"}");
    return;
  }

  cleanup(millis());
  for (auto &pending : pendingClaims) {
    if (!pending.used || pending.requestId != id) {
      continue;
    }

    if (!pending.approved) {
      sendJson(server, 200, "{\"ok\":true,\"status\":\"pending\"}");
      return;
    }

    if (pending.error[0]) {
      const String error(pending.error); pending = PendingClaim{};
      sendJson(server, 409, String("{\"error\":\"") + jsonEscape(error) + "\"}"); return;
    }

    String response = "{\"ok\":true,\"status\":\"approved\",\"module\":";
    response += String(pending.controllerId);
    response += ",\"slot\":";
    response += String(pending.slot);
    response += ",\"token\":\"";
    response += pending.token;
    response += "\"}";
    pending = PendingClaim{};
    sendJson(server, 200, response);
    return;
  }

  sendJson(server, 404, "{\"ok\":false,\"status\":\"expired\",\"error\":\"Claim expired or was already collected\"}");
}

void handleSessionLogin(WebServer &server) {
  if (server.hasArg("profileId")) {
    const String id = server.arg("profileId"), pin = server.arg("pin");
    if (!validPin(pin) || !TurnHubProfiles::profileExists(id)) {
      sendJson(server, 401, "{\"error\":\"Profile or PIN was not accepted\"}"); return;
    }
    if (!loginLimiter.allow(id.c_str(), millis())) {
      sendJson(server, 429, "{\"error\":\"Too many attempts. Wait 30 seconds and retry\"}"); return;
    }
    const String stored = TurnHubProfiles::storedPinHashForProfile(id);
    if (stored.length() != 64 || !stored.equalsIgnoreCase(profilePinHash(id, pin))) {
      sendJson(server, 401, "{\"error\":\"Profile or PIN was not accepted. Older profiles may need physical sign-in once\"}"); return;
    }
    loginLimiter.success(id.c_str());
    TurnHubAccounts::Account reconnect;
    if(!TurnHubAccounts::load(id,reconnect)||reconnect.archived){sendJson(server,403,"{\"error\":\"Account unavailable or archived\"}");return;}
    if(reconnect.reconnectRequired){reconnect.reconnectRequired=false;if(!TurnHubAccounts::save(id,reconnect)){sendJson(server,503,"{\"error\":\"Could not reconnect\"}");return;}}
    sendLogin(server, createProfileSession(id, millis())); return;
  }
  if (!server.hasArg("module") || !server.hasArg("slot") || !server.hasArg("pin")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Module, slot, and PIN are required\"}");
    return;
  }

  const int module = server.arg("module").toInt();
  const int slot = server.arg("slot").toInt();
  SeatSnapshot snapshot;
  if (module < 0 || module >= MAX_PHYSICAL_SIGILS || !validSlot(slot) ||
      !resolveSeatNow(static_cast<uint8_t>(module), static_cast<uint8_t>(slot), snapshot)) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Seat is not available\"}");
    return;
  }

  const String loginProfile = profileIdForPhysicalSeat(static_cast<uint8_t>(module), static_cast<uint8_t>(slot));
  if (!loginLimiter.allow(loginProfile.c_str(), millis())) {
    sendJson(server, 429, "{\"error\":\"Too many attempts. Wait 30 seconds and retry\"}"); return;
  }
  if (!validPin(server.arg("pin")) || !pinMatches(
          static_cast<uint8_t>(module),
          static_cast<uint8_t>(slot),
          server.arg("pin"))) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Incorrect PIN\"}");
    return;
  }
  loginLimiter.success(loginProfile.c_str());
  TurnHubAccounts::Account reconnect;
  if(!TurnHubAccounts::load(loginProfile,reconnect)||reconnect.archived){sendJson(server,403,"{\"error\":\"Account unavailable or archived\"}");return;}
  if(reconnect.reconnectRequired){reconnect.reconnectRequired=false;if(!TurnHubAccounts::save(loginProfile,reconnect)){sendJson(server,503,"{\"error\":\"Could not reconnect\"}");return;}}

  WebSession *session = createSession(
      static_cast<uint8_t>(module),
      static_cast<uint8_t>(slot),
      millis());
  if (session == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"No session slots available\"}");
    return;
  }

  String response = "{\"ok\":true,\"token\":\"";
  response += session->token;
  response += "\",\"module\":";
  response += String(module);
  response += ",\"slot\":";
  response += String(slot);
  response += ",\"profileId\":\"";
  response += jsonEscape(sessionProfileId(*session));
  response += "\"}";
  sendJson(server, 200, response);
}

void handleSessionMe(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"authenticated\":false}");
    return;
  }

  SeatSnapshot snapshot;
  const bool participating = resolveSessionParticipant(*session) &&
      resolveSeatNow(session->controllerId, session->slot, snapshot);

  const SigilRecord *record = recordForModule(session->controllerId);
  const String profileId = sessionProfileId(*session);
  const String savedName = TurnHubProfiles::nameForProfile(profileId);

  String response = "{\"ok\":true,\"authenticated\":true,\"module\":";
  response += String(session->controllerId);
  TurnHubAccounts::Account account;
  const bool accountReady=TurnHubAccounts::load(profileId,account);
  response += ",\"permissions\":";response += String(accountReady?account.permissions:0);
  response += ",\"participating\":"; response += participating ? "true" : "false";
  response += ",\"host\":"; response += snapshot.host ? "true" : "false";
  response += ",\"virtual\":"; response += session->controllerId >= MAX_PHYSICAL_SIGILS ? "true" : "false";
  response += ",\"slot\":";
  response += String(session->slot);
  response += ",\"slotName\":\"";
  response += session->slot == 1 ? "A" : "B";
  response += "\",\"player\":";
  response += String(snapshot.playerNumber);
  response += ",\"active\":";
  response += snapshot.active ? "true" : "false";
  response += ",\"eliminated\":";
  response += snapshot.eliminated ? "true" : "false";
  response += ",\"lifeAvailable\":"; response += snapshot.lifeAvailable ? "true" : "false";
  response += ",\"life\":"; response += String(snapshot.life);
  response += ",\"profileId\":\"";
  response += jsonEscape(profileId);
  response += "\",\"statsUrl\":\"/stats\",\"name\":\"";
  response += jsonEscape(savedName);
  response += "\",\"hasPin\":";
  response += TurnHubProfiles::hasPinForProfile(profileId) ? "true" : "false";
  TurnHubProfiles::ProfilePolicy policy;
  const bool policyAvailable = TurnHubProfiles::loadPolicyForProfile(profileId, policy);
  response += ",\"policyAvailable\":"; response += policyAvailable ? "true" : "false";
  if (policyAvailable) {
    response += ",\"allowPhysicalWithoutPin\":"; response += policy.allowPhysicalWithoutPin ? "true" : "false";
    response += ",\"hideStatsWithoutAuthentication\":"; response += policy.hideStatsWithoutAuthentication ? "true" : "false";
  }
  if (record != nullptr) {
    response += ",\"hardwareId\":\"";
    response += sigilHardwareId(record->mac);
    response += '"';
  }
  response += '}';
  sendJson(server, 200, response);
}

void handleProfile(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return;
  }

  const String profileId = sessionProfileId(*session);
  if(server.hasArg("clearPin")){
    TurnHubAccounts::Account account;
    if(!TurnHubAccounts::load(profileId,account)||account.permissions||account.reconnectRequired){sendJson(server,403,"{\"error\":\"This account must keep a PIN\"}");return;}
  }
  if (!profileStoreReady || profileId.length() == 0) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Profile storage unavailable\"}");
    return;
  }

  SigilBus *bus = SigilBus::activeInstance();
  resolveSessionParticipant(*session);
  const SigilRecord *record = recordForModule(session->controllerId);
  bool displayProfileChanged = false;

  if (server.hasArg("name")) {
    String name = server.arg("name");
    name.trim();
    if (name.length() > 32) {
      name.remove(32);
    }
    if (!TurnHubProfiles::setNameForProfile(profileId, name)) {
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
    if (hash.length() != 64 ||
        !TurnHubProfiles::setPinHashForProfile(profileId, hash)) {
      sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save PIN\"}");
      return;
    }
    if (record != nullptr) {
      TurnHubProfiles::setPinHashForSeat(record->mac, session->slot, hash);
    }
  }

  if (server.hasArg("clearPin") && server.arg("clearPin") == "1") {
    TurnHubProfiles::ProfilePolicy policy;
    if (!TurnHubProfiles::loadPolicyForProfile(profileId, policy) || !policy.allowPhysicalWithoutPin) {
      sendJson(server, 409, "{\"error\":\"Enable PIN-free physical use before removing the PIN\"}"); return;
    }
    if (record == nullptr) {
      sendJson(server, 409, "{\"error\":\"Keep a PIN for hardware-independent profile login\"}"); return;
    }
    if (!TurnHubProfiles::clearPinForProfile(profileId)) {
      sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not remove PIN\"}");
      return;
    }
    if (record != nullptr) {
      TurnHubProfiles::clearPinForSeat(record->mac, session->slot);
    }
  }

  if (server.hasArg("pin") || (server.hasArg("clearPin") && server.arg("clearPin") == "1")) {
    for (auto &other : sessions) {
      if (&other != session && other.used && String(other.profileId) == profileId) other = WebSession{};
    }
  }

  if (
      displayProfileChanged &&
      record != nullptr &&
      bus != nullptr &&
      record->helloInfoValid &&
      (record->capabilities & TurnHubProtocol::CAPABILITY_DISPLAY_PROFILE) != 0) {
    bus->syncDisplayProfile(session->controllerId);
  }

  sendJson(server, 200, "{\"ok\":true}");
}

bool parseLifeInteger(const String &text, int32_t &value, bool negativeAllowed) {
  if (!text.length() || text.length() > 8) return false;
  const bool negative = text[0] == '-';
  size_t i = negative ? 1 : 0;
  if ((negative && !negativeAllowed) || i == text.length()) return false;
  uint32_t number=0;
  for (; i<text.length(); ++i) {
    if (text[i]<'0'||text[i]>'9') return false;
    number=number*10+static_cast<uint32_t>(text[i]-'0');
    if (number>1000000) return false;
  }
  value=negative?-static_cast<int32_t>(number):static_cast<int32_t>(number);return true;
}

void handleGameSettings(WebServer &server) {
  TurnHub::GameSettings settings;bool editable=false;
  const bool available=readGameConfiguration&&readGameConfiguration(settings,editable);
  auto *session=sessionForRequest(server);SeatSnapshot seat;
  const bool host=session&&resolveSessionParticipant(*session)&&resolveSeatNow(session->controllerId,session->slot,seat)&&seat.host&&session->slot==1;
  String presets;
  for (uint8_t i=0;i<TurnHub::TURN_TIMER_PRESET_COUNT;++i) {
    if (i) presets+=',';
    presets+=String(TurnHub::TURN_TIMER_PRESETS_MS[i]);
  }
  String json=String("{\"gameProfile\":\"")+TurnHub::gameProfileKey(settings.profile)+
      "\",\"startingLife\":"+String(settings.startingLife)+
      ",\"turnTimerMs\":"+String(settings.turnTimerMs)+
      ",\"turnTimer\":{\"presetsMs\":["+presets+"],\"minMs\":"+String(TurnHub::TURN_TIMER_MIN_MS)+
      ",\"maxMs\":"+String(TurnHub::TURN_TIMER_MAX_MS)+",\"warningMs\":"+String(TurnHub::TURN_TIMER_WARNING_MS)+
      ",\"longTurnMs\":"+String(TurnHub::TURN_TIMER_LONG_TURN_MS)+"}"+
      ",\"available\":"+(available?"true":"false")+
      ",\"canEdit\":"+(available&&editable&&host?"true":"false")+"}";
  sendJson(server,200,json);
}

// Whole milliseconds, 0 (OFF) or within the Atlas range; Atlas validates again.
bool parseTurnTimer(const String &text, uint32_t &value) {
  if (!text.length() || text.length() > 7) return false;
  uint32_t number=0;
  for (size_t i=0;i<text.length();++i) {
    if (text[i]<'0'||text[i]>'9') return false;
    number=number*10+static_cast<uint32_t>(text[i]-'0');
  }
  value=number;
  return TurnHub::validTurnTimerMs(number);
}

void handleSaveGameSettings(WebServer &server) {
  auto *session=sessionForRequest(server);
  if (!session) {sendJson(server,401,"{\"error\":\"Sign in first\"}");return;}
  // Omitted fields keep their current value, so a client can change one setting.
  TurnHub::GameSettings settings;bool editable=false;
  if (readGameConfiguration) readGameConfiguration(settings,editable);
  if ((server.hasArg("gameProfile")&&!TurnHub::parseGameProfile(server.arg("gameProfile").c_str(),settings.profile))||
      (server.hasArg("startingLife")&&!parseLifeInteger(server.arg("startingLife"),settings.startingLife,false))) {
    sendJson(server,400,"{\"error\":\"Choose a valid game profile and starting life from 0 to 1000000\"}");return;
  }
  if (server.hasArg("turnTimerMs")&&!parseTurnTimer(server.arg("turnTimerMs"),settings.turnTimerMs)) {
    sendJson(server,400,"{\"error\":\"Turn timer must be off or 15 seconds to 60 minutes in whole seconds\"}");return;
  }
  String message="Join the table first";
  if (!resolveSessionParticipant(*session)||!configureGameHandler||
      !configureGameHandler(session->controllerId,session->slot,settings,message)) {
    sendJson(server,409,String("{\"error\":\"")+jsonEscape(message)+"\"}");return;
  }
  sendJson(server,200,"{\"ok\":true}");
}

void handleChangeLife(WebServer &server) {
  auto *session=sessionForRequest(server);
  if (!session) {sendJson(server,401,"{\"error\":\"Sign in first\"}");return;}
  int32_t delta=0;
  if (!parseLifeInteger(server.arg("delta"),delta,true)||delta==0) {
    sendJson(server,400,"{\"error\":\"Enter a nonzero life change between -1000000 and 1000000\"}");return;
  }
  String message="Join the table first";
  if (!resolveSessionParticipant(*session)||!changeLifeHandler||
      !changeLifeHandler(session->controllerId,session->slot,delta,message)) {
    sendJson(server,409,String("{\"error\":\"")+jsonEscape(message)+"\"}");return;
  }
  sendJson(server,200,"{\"ok\":true}");
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

void handleCounters(WebServer &server) {
  auto *session = sessionForRequest(server);
  if (!session) { sendJson(server,401,"{\"error\":\"Sign in first\"}"); return; }
  CounterSnapshot snapshot;
  if (!resolveSessionParticipant(*session) || !readCountersHandler ||
      !readCountersHandler(session->controllerId, session->slot, snapshot)) {
    sendJson(server,200,"{\"available\":false,\"requests\":[],\"damage\":[]}"); return;
  }
  String json = String("{\"available\":true,\"editable\":") + (snapshot.editable ? "true" : "false") +
      ",\"commanderEnabled\":" + (snapshot.commanderEnabled ? "true" : "false") +
      ",\"player\":" + String(snapshot.player) + ",\"requests\":[";
  bool first = true;
  const uint32_t now = millis();
  for (const auto &request : snapshot.requests) {
    if (!request.id) continue;
    if (!first) json += ',';
    first = false;
    const uint32_t age = now - request.requestedAtMs;
    const uint32_t remaining = request.state == TurnHub::LifeChangeState::Pending && age < TurnHub::LIFE_APPROVAL_MS
        ? TurnHub::LIFE_APPROVAL_MS - age : 0;
    json += String("{\"id\":") + String(request.id) + ",\"actor\":" + String(request.actor) +
        ",\"target\":" + String(request.target) + ",\"delta\":" + String(request.delta) +
        ",\"state\":\"" + lifeChangeStateName(request.state) + "\",\"remainingMs\":" + String(remaining) + "}";
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
  sendJson(server,200,json);
}

void handleCounterControl(WebServer &server, TurnHub::IntentType type) {
  auto *session = sessionForRequest(server);
  if (!session) { sendJson(server,401,"{\"error\":\"Sign in first\"}"); return; }
  TurnHub::IntentPayload payload;
  int32_t number = 0;
  bool valid = true;
  if (type == TurnHub::IntentType::RespondLifeChange) {
    const String id = server.arg("requestId"), accept = server.arg("accept");
    uint64_t parsed = 0;
    valid = id.length() > 0 && id.length() <= 10 && (accept == "0" || accept == "1");
    for (size_t i = 0; valid && i < id.length(); ++i) {
      valid = id[i] >= '0' && id[i] <= '9';
      if (valid) parsed = parsed * 10 + static_cast<uint8_t>(id[i] - '0');
    }
    valid = valid && parsed > 0 && parsed <= UINT32_MAX;
    payload.requestId = static_cast<uint32_t>(parsed);
    payload.flags = accept == "1" ? 1 : 0;
  } else {
    valid = parseLifeInteger(server.arg("delta"), payload.value, true) && payload.value != 0;
    if (type == TurnHub::IntentType::RequestLifeChange) {
      valid = valid && parseLifeInteger(server.arg("target"), number, false) && number >= 1 && number <= MAX_PLAYERS;
      payload.targetPlayer = static_cast<uint8_t>(number);
    } else {
      valid = valid && parseLifeInteger(server.arg("source"), number, false) && number >= 1 && number <= MAX_PLAYERS;
      payload.counterSource = static_cast<uint8_t>(number);
      valid = valid && parseLifeInteger(server.arg("commander"), number, false) && number >= 1 && number <= TurnHub::COMMANDERS_PER_PLAYER;
      payload.counterSlot = static_cast<uint8_t>(number);
    }
  }
  if (!valid) { sendJson(server,400,"{\"error\":\"Invalid life or Commander request\"}"); return; }
  String message = "Join the table first";
  if (!resolveSessionParticipant(*session) || !counterControlHandler ||
      !counterControlHandler(session->controllerId, session->slot, type, payload, message)) {
    sendJson(server,409,String("{\"error\":\"") + jsonEscape(message) + "\"}"); return;
  }
  sendJson(server,200,String("{\"ok\":true,\"message\":\"") + jsonEscape(message) + "\"}");
}

void handleProfilePolicy(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendJson(server, 401, "{\"error\":\"Sign into your profile to change its settings\"}"); return;
  }
  const String id = sessionProfileId(*session);
  const String physical = server.arg("allowPhysicalWithoutPin");
  const String stats = server.arg("hideStatsWithoutAuthentication");
  if ((physical != "0" && physical != "1") || (stats != "0" && stats != "1")) {
    sendJson(server, 400, "{\"error\":\"Both profile choices must be 0 or 1\"}"); return;
  }
  if (physical == "0" && !TurnHubProfiles::hasPinForProfile(id)) {
    sendJson(server, 409, "{\"error\":\"Set a PIN before requiring authentication for physical use\"}"); return;
  }
  TurnHubProfiles::ProfilePolicy policy;
  policy.allowPhysicalWithoutPin = physical == "1";
  policy.hideStatsWithoutAuthentication = stats == "1";
  if (!TurnHubProfiles::savePolicyForProfile(id, policy)) {
    sendJson(server, 503, "{\"error\":\"Profile settings could not be saved; reload before retrying\"}"); return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

void handleProfileStats(WebServer &server) {
  WebSession *session = nullptr;
  String profileId;
  String name;
  ProfileStats stats{};
  if (!loadSessionStats(server, session, profileId, name, stats)) {
    return;
  }

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
  response += "}}";
  sendJson(server, 200, response);
}

void handleProfileStatsExport(WebServer &server) {
  WebSession *session = nullptr;
  String profileId;
  String name;
  ProfileStats stats{};
  if (!loadSessionStats(server, session, profileId, name, stats)) {
    return;
  }

  const String report =
      TurnHubProfileStats::buildTextReport(profileId, name, stats);
  const String filename = safeExportName(name, profileId);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader(
      "Content-Disposition",
      String("attachment; filename=\"") + filename + "\"");
  server.send(200, "text/plain; charset=utf-8", report);
}

// Recent serial output as a text file, so a table without a USB cable can
// still hand over a log. The ring is RAM-only (see serial_log.h).
void handleSerialLogDownload(WebServer &server) {
  server.sendHeader("Cache-Control", "no-store");
  if (!requirePermission(server, TurnHubAccounts::Developer)) return;
  const String atlasId = atlasHardwareId();
  const String captured = serialLog.snapshot();
  String body;
  body.reserve(captured.length() + 256);
  body = "# TurnHub Atlas serial log\n# atlasId=";
  body += atlasId;
  body += " bootId=";
  body += bootId;
  body += " firmware=";
  body += TurnHubFirmware::VERSION;
  body += " uptimeMs=";
  body += String(millis());
  body += "\n# Lines are stamped with Atlas uptime in seconds. RAM only: cleared on reboot.\n";
  const uint32_t dropped = serialLog.droppedBytes();
  if (dropped > 0) {
    body += "# Earlier output dropped: ";
    body += String(dropped);
    body += " bytes did not fit in the ";
    body += String(static_cast<uint32_t>(TurnHub::SerialLog::CAPACITY));
    body += "-byte buffer.\n";
  }
  body += captured;
  server.sendHeader(
      "Content-Disposition",
      String("attachment; filename=\"turnhub-") + atlasId + "-" + String(bootId).substring(0, 8) + ".log\"");
  server.send(200, "text/plain; charset=utf-8", body);
}

void handleLogout(WebServer &server) {
  const String token = server.header("X-TurnHub-Token");
  for (auto &session : sessions) {
    if (session.used && token.equalsIgnoreCase(session.token)) {
      session = WebSession{};
      break;
    }
  }
  sendJson(server, 200, "{\"ok\":true}");
}

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

void runControl(WebServer &server, WebControl control) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return;
  }

  SeatSnapshot snapshot;
  if (!resolveSessionParticipant(*session) || !resolveSeatNow(session->controllerId, session->slot, snapshot)) {
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
    if (!readClientRevision || server.arg("expectedBootId") != bootId ||
        value != readClientRevision()) {
      sendControlResult(server, 409, "CONFLICT", "Fetch a fresh state snapshot");
      return;
    }
  }

  String message;
  if (!controlHandler(session->controllerId, session->slot, control, message)) {
    if (message.length() == 0) {
      message = "Control is not available right now";
    }
    sendControlResult(server, 409, "REJECTED", message);
    return;
  }

  if (message.length() == 0) {
    message = "Control accepted";
  }
  sendControlResult(server, 200, "ACCEPTED", message);
}

}  // namespace

void configureClientState(StateCallback state, RevisionCallback revision) {
  readClientState = state;
  readClientRevision = revision;
}

bool profileAuthenticated(const String &profileId) {
  cleanup(millis());
  if (profileId.length() == 0) return false;
  for (const auto &session : sessions) {
    if (session.used && profileId == session.profileId) return true;
  }
  return false;
}

bool physicalUseAllowed(const String &profileId) {
  // Guest participation needs no durable identity. Bound accounts still use
  // their saved authentication and moderation policy below.
  if (profileId.length() == 0) return true;
  if(connectionBlocked(profileId))return false;
  TurnHubProfiles::ProfilePolicy policy;
  return TurnHubProfiles::loadPolicyForProfile(profileId, policy) &&
      (policy.allowPhysicalWithoutPin || profileAuthenticated(profileId));
}

bool physicalStatsVisible(const String &profileId) {
  TurnHubProfiles::ProfilePolicy policy;
  return TurnHubProfiles::loadPolicyForProfile(profileId, policy) &&
      (!policy.hideStatsWithoutAuthentication || profileAuthenticated(profileId));
}

void configureGameControls(GameSettingsCallback read, ConfigureGameCallback configure, ChangeLifeCallback life) {
  readGameConfiguration=read;configureGameHandler=configure;changeLifeHandler=life;
}

void configureCounterControls(ReadCountersCallback read, CounterControlCallback control) {
  readCountersHandler = read;
  counterControlHandler = control;
}

void configure(
    ResolveSeatCallback resolveSeatCallback,
    ControlCallback controlCallback,
    ProfileControlCallback profileControlCallback,
    ResolveProfileCallback resolveProfileCallback) {
  resolveSeat = resolveSeatCallback;
  controlHandler = controlCallback;
  profileControlHandler = profileControlCallback;
  resolveProfile = resolveProfileCallback;
}

void notePhysicalAction(uint8_t sigilId) {
  const uint32_t nowMs = millis();
  cleanup(nowMs);

  PendingClaim *oldest = nullptr;
  for (auto &pending : pendingClaims) {
    if (!pending.used || pending.approved || pending.controllerId != sigilId) {
      continue;
    }
    if (oldest == nullptr || pending.createdMs < oldest->createdMs) {
      oldest = &pending;
    }
  }

  if (oldest == nullptr) {
    return;
  }

  SeatSnapshot snapshot;
  WebSession *requester = oldest->requestingToken[0] ?
      sessionForToken(String(oldest->requestingToken), nowMs) : nullptr;
  if (!oldest->requestingToken[0] &&
      (hasPin(oldest->controllerId, oldest->slot) ||
       profileIdForPhysicalSeat(oldest->controllerId, oldest->slot) != oldest->profileId)) {
    oldest->approved = true;
    strncpy(oldest->error, "Profile changed or requires PIN login; sign in again", sizeof(oldest->error) - 1);
    return;
  }
  if (oldest->requestingToken[0]) {
    String message = "Sign-in expired; request attachment again";
    if (!requester || !profileControlHandler ||
        !profileControlHandler(sessionProfileId(*requester), WebControl::AttachPhysical,
            oldest->controllerId, oldest->slot, message)) {
      oldest->approved = true;
      strncpy(oldest->error, message.c_str(), sizeof(oldest->error) - 1);
      return;
    }
  }
  if (!resolveSeatNow(oldest->controllerId, oldest->slot, snapshot)) {
    *oldest = PendingClaim{};
    return;
  }

  WebSession *session = requester ? requester : createSession(oldest->controllerId, oldest->slot, nowMs);
  if (session == nullptr) {
    return;
  }

  oldest->approved = true;
  strncpy(oldest->token, session->token, sizeof(oldest->token) - 1);
  oldest->token[sizeof(oldest->token) - 1] = '\0';

  serialLog.print("ATLAS|WEB_SESSION|AUTHORIZED|SIGIL|");
  serialLog.print(oldest->controllerId);
  serialLog.print("|SLOT|");
  serialLog.print(oldest->slot == 1 ? 'A' : 'B');
  serialLog.print("|PROFILE|");
  serialLog.println(session->profileId);
}


void configureModeration(ModerateCallback cb){moderateHandler=cb;}
bool connectionBlocked(const String &id){TurnHubAccounts::Account a;return TurnHubAccounts::load(id,a)&&(a.archived||a.reconnectRequired);}
void revokeConnections(const String &id){
  for(auto &s:sessions)if(s.used&&id==s.profileId){
    for(auto &p:pendingClaims)if(p.used&&!strcmp(p.requestingToken,s.token))p=PendingClaim{};
    s=WebSession{};
  }
  for(auto &p:pendingClaims)if(p.used&&id==p.profileId)p=PendingClaim{};
}
bool requirePermission(WebServer &server,uint8_t permission){
  auto *s=sessionForRequest(server);
  if(!s){sendJson(server,401,"{\"error\":\"Sign in first\"}");return false;}
  if(!TurnHubAccounts::has(String(s->profileId),permission)){sendJson(server,403,"{\"error\":\"Account permission required\"}");return false;}
  return true;
}
void serveRestrictedPage(WebServer &server,const char *html,uint8_t permission){
  server.sendHeader("Cache-Control","no-store");
  if(!server.header("X-TurnHub-Token").length()){
    server.send(200,"text/html",R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><p id="m">Checking account access…</p><a href="/portal">Back to portal</a><script>fetch(location.pathname,{headers:{'X-TurnHub-Token':localStorage.getItem('turnhubSessionToken')||''}}).then(async r=>{if(!r.ok)throw Error('Access denied. Sign in with the required account permission.');const t=await r.text();if(!localStorage.getItem('turnhubSessionToken'))throw Error('Sign in first.');document.open();document.write(t);document.close()}).catch(e=>document.getElementById('m').textContent=e.message)</script>)HTML");return;
  }
  if(requirePermission(server,permission))server.send_P(200,"text/html",html);
}
void handleAccountSetup(WebServer &server,bool readOnly=false){
  String primary;if(!TurnHubAccounts::primaryAdmin(primary)){sendJson(server,503,"{\"error\":\"Account storage unavailable\"}");return;}
  if(readOnly){sendJson(server,200,String("{\"setupRequired\":")+(primary.length()?"false}":"true}"));return;}
  if(primary.length()){sendJson(server,409,"{\"error\":\"Admin setup is already complete\"}");return;}
  auto *s=sessionForRequest(server);if(!s){sendJson(server,401,"{\"error\":\"Create or sign into your account first\"}");return;}
  if(!requireMasterButton(server))return;
  if(!TurnHubAccounts::establishAdmin(String(s->profileId))){sendJson(server,503,"{\"error\":\"Could not establish Admin; a saved PIN is required\"}");return;}
  sendJson(server,200,"{\"ok\":true}");
}
void handleAccounts(WebServer &server){
  auto *s=sessionForRequest(server);if(!s){sendJson(server,401,"{\"error\":\"Sign in first\"}");return;}
  TurnHubAccounts::Account actor;if(!TurnHubAccounts::load(String(s->profileId),actor)){sendJson(server,503,"{\"error\":\"Account unavailable\"}");return;}
  const bool admin=actor.permissions&TurnHubAccounts::Admin,gm=actor.permissions&TurnHubAccounts::GameMaster;
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][9];const size_t count=TurnHubProfiles::listProfileIds(ids,TurnHubProfiles::MAX_LOGIN_PROFILES);
  String json="{\"accounts\":[";bool comma=false;
  for(size_t i=0;i<count;++i){
    const String id(ids[i]);if(!admin&&!gm&&id!=s->profileId)continue;
    TurnHubAccounts::Account a;if(!TurnHubAccounts::load(id,a))continue;
    if(a.archived&&!admin)continue;
    if(comma)json+=',';comma=true;
    json+="{\"profileId\":\""+id+"\",\"name\":\""+jsonEscape(TurnHubProfiles::nameForProfile(id))+"\",\"permissions\":"+String(a.permissions);
    json+=",\"archived\":";json+=a.archived?"true":"false";
    if(gm||id==s->profileId){json+=",\"connectionResets\":"+String(a.connectionResets)+",\"gameRemovals\":"+String(a.gameRemovals)+",\"nudgeMuted\":"+(a.nudgeMuted?"true":"false");}
    json+='}';
  }json+="]}";sendJson(server,200,json);
}
void handleAccountPermissions(WebServer &server){
  if(!requirePermission(server,TurnHubAccounts::Admin))return;
  const String id=server.arg("profileId"),raw=server.arg("permissions");
  const int flags=raw.toInt();
  if(raw!=String(flags)||flags<0||flags>31||((flags&24)&&!(flags&TurnHubAccounts::GameMaster))){sendJson(server,400,"{\"error\":\"Invalid permissions\"}");return;}
  String primary;TurnHubAccounts::Account a;
  if(!TurnHubAccounts::primaryAdmin(primary)||!TurnHubAccounts::load(id,a)){sendJson(server,503,"{\"error\":\"Account unavailable\"}");return;}
  if((id==primary&&!(flags&TurnHubAccounts::Admin))||(flags&&!TurnHubProfiles::hasPinForProfile(id))){sendJson(server,409,"{\"error\":\"Keep the initial Admin and a PIN on privileged accounts\"}");return;}
  a.permissions=uint8_t(flags);if(!TurnHubAccounts::save(id,a)){sendJson(server,503,"{\"error\":\"Could not save permissions\"}");return;}
  sendJson(server,200,"{\"ok\":true}");
}
void handleAccountArchive(WebServer &server){
  if(!requirePermission(server,TurnHubAccounts::Admin))return;
  const String id=server.arg("profileId"),value=server.arg("archived");
  if(value!="0"&&value!="1"){sendJson(server,400,"{\"error\":\"Choose archive or restore\"}");return;}
  String primary;TurnHubAccounts::Account account;
  if(!TurnHubAccounts::primaryAdmin(primary)||!TurnHubAccounts::load(id,account)){sendJson(server,503,"{\"error\":\"Account unavailable\"}");return;}
  const bool archive=value=="1";
  if(archive&&id==primary){sendJson(server,409,"{\"error\":\"The initial Admin cannot be archived\"}");return;}
  uint8_t controller=INVALID_ID,slot=1;
  if(archive&&resolveProfile&&resolveProfile(id,controller,slot)){sendJson(server,409,"{\"error\":\"Account is still at the table. Leave the table or reset the completed game before archiving\"}");return;}
  if(account.archived!=archive){
    account.archived=archive;
    if(!TurnHubAccounts::save(id,account)){sendJson(server,503,"{\"error\":\"Could not save archive state\"}");return;}
  }
  if(archive)revokeConnections(id);
  sendJson(server,200,"{\"ok\":true}");
}
void handleModerate(WebServer &server){
  if(!requirePermission(server,TurnHubAccounts::GameMaster))return;
  auto *s=sessionForRequest(server);const String actor(s->profileId);String message;
  if(!moderateHandler||!moderateHandler(actor,server.arg("profileId"),server.arg("action"),message)){sendJson(server,409,String("{\"error\":\"")+jsonEscape(message)+"\"}");return;}
  sendJson(server,200,"{\"ok\":true}");
}
void begin(WebServer &server) {
  server.on("/api/accounts/setup",HTTP_GET,[&server](){handleAccountSetup(server,true);});
  server.on("/api/accounts/setup",HTTP_POST,[&server](){handleAccountSetup(server);});
  server.on("/api/accounts",HTTP_GET,[&server](){handleAccounts(server);});
  server.on("/api/accounts/permissions",HTTP_POST,[&server](){handleAccountPermissions(server);});
  server.on("/api/accounts/archive",HTTP_POST,[&server](){handleAccountArchive(server);});
  server.on("/api/accounts/moderate",HTTP_POST,[&server](){handleModerate(server);});

  if (webServer != nullptr) {
    return;
  }
  webServer = &server;
  makeToken(bootId); // Public boot epoch, not an authentication credential.
  profileStoreReady = TurnHubProfiles::begin();

  static const char *headerKeys[] = {"X-TurnHub-Token"};
  server.collectHeaders(headerKeys, 1);

  server.on("/api/v1/info", HTTP_GET, [&server]() {
    String json;
    json.reserve(640);
    json = "{\"product\":\"TurnHub\",\"deviceType\":\"Atlas\",\"atlasId\":\"";
    json += atlasHardwareId(); json += "\",\"firmwareVersion\":\"";
    json += TurnHubFirmware::VERSION;
    json += "\",\"apiVersion\":\"1\",\"protocolVersion\":\"0.1\",\"radioProtocolVersion\":";
    json += String(TurnHubProtocol::VERSION);
    json += ",\"bootId\":\""; json += bootId; json += "\",\"revision\":";
    json += String(readClientRevision ? readClientRevision() : 0);
    json += ",\"capabilities\":{\"stateSnapshot\":";
    json += readClientState && readClientRevision ? "true" : "false";
    json += ",\"sessionControls\":true,\"intentEnvelope\":false,\"events\":false,\"requestDeduplication\":false}}";
    sendJson(server, 200, json);
  });
  server.on("/api/v1/state", HTTP_GET, [&server]() {
    if (!readClientState || !readClientRevision) {
      sendJson(server, 503, "{\"error\":\"State snapshot is not configured\"}");
      return;
    }
    sendJson(server, 200, readClientState(atlasHardwareId(), bootId));
  });

  server.on("/api/diagnostics/log", HTTP_GET, [&server]() { handleSerialLogDownload(server); });
  server.on("/stats", HTTP_GET, [&server]() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", TurnHubStatsPage::STATS_HTML);
  });
  server.on("/login", HTTP_GET, [&server]() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", TurnHubLoginPage::HTML);
  });

  server.on("/api/devices", HTTP_GET, [&server]() { handleDevices(server); });
  server.on("/api/device/persistence", HTTP_POST, [&server]() { handleSeatPersistence(server); });
  server.on("/api/device/name", HTTP_POST, [&server]() { handleDeviceName(server); });
  server.on("/api/network", HTTP_GET, [&server]() { handleNetworkInfo(server); });
  server.on("/api/network/password", HTTP_POST, [&server]() { handleNetworkPassword(server); });
  server.on("/api/seats", HTTP_GET, [&server]() { handleSeats(server); });
  server.on("/api/profiles", HTTP_GET, [&server]() { handleProfiles(server); });
  server.on("/api/profiles/register", HTTP_POST, [&server]() { handleRegistration(server); });
  server.on("/api/session/join", HTTP_POST, [&server]() { handleParticipation(server, WebControl::Join); });
  server.on("/api/session/leave", HTTP_POST, [&server]() { handleParticipation(server, WebControl::Leave); });
  server.on("/api/session/request", HTTP_POST, [&server]() { handleSessionRequest(server); });
  server.on("/api/session/poll", HTTP_GET, [&server]() { handleSessionPoll(server); });
  server.on("/api/session/login", HTTP_POST, [&server]() { handleSessionLogin(server); });
  server.on("/api/session/me", HTTP_GET, [&server]() { handleSessionMe(server); });
  server.on("/api/session/profile", HTTP_POST, [&server]() { handleProfile(server); });
  server.on("/api/session/policy", HTTP_POST, [&server]() { handleProfilePolicy(server); });
  server.on("/api/game/settings", HTTP_GET, [&server]() { handleGameSettings(server); });
  server.on("/api/game/settings", HTTP_POST, [&server]() { handleSaveGameSettings(server); });
  server.on("/api/control/life", HTTP_POST, [&server]() { handleChangeLife(server); });
  server.on("/api/game/counters", HTTP_GET, [&server]() { handleCounters(server); });
  server.on("/api/control/life/request", HTTP_POST, [&server]() { handleCounterControl(server,TurnHub::IntentType::RequestLifeChange); });
  server.on("/api/control/life/respond", HTTP_POST, [&server]() { handleCounterControl(server,TurnHub::IntentType::RespondLifeChange); });
  server.on("/api/control/commander", HTTP_POST, [&server]() { handleCounterControl(server,TurnHub::IntentType::ChangeCounter); });
  server.on("/api/session/stats", HTTP_GET, [&server]() { handleProfileStats(server); });
  server.on("/api/session/stats/export", HTTP_GET, [&server]() { handleProfileStatsExport(server); });
  server.on("/api/session/logout", HTTP_POST, [&server]() { handleLogout(server); });

  server.on("/api/control/pass", HTTP_POST, [&server]() { runControl(server, WebControl::Pass); });
  server.on("/api/control/pause", HTTP_POST, [&server]() { runControl(server, WebControl::PauseResume); });
  server.on("/api/control/concede", HTTP_POST, [&server]() { runControl(server, WebControl::Concede); });
  server.on("/api/control/win", HTTP_POST, [&server]() { runControl(server, WebControl::ClaimWin); });
  server.on("/api/control/confirm", HTTP_POST, [&server]() { runControl(server, WebControl::ConfirmWin); });
  server.on("/api/control/deny", HTTP_POST, [&server]() { runControl(server, WebControl::DenyWin); });
  server.on("/api/control/starter", HTTP_POST, [&server]() { runControl(server, WebControl::SelectStarter); });
  server.on("/api/control/start", HTTP_POST, [&server]() { runControl(server, WebControl::Start); });
  server.on("/api/control/cancel-start", HTTP_POST, [&server]() { runControl(server, WebControl::CancelStart); });
  server.on("/api/control/rematch", HTTP_POST, [&server]() { runControl(server, WebControl::Rematch); });
  server.on("/api/control/reset", HTTP_POST, [&server]() { runControl(server, WebControl::Reset); });

  serialLog.print("ATLAS|WEB_API|READY|PROFILES|");
  serialLog.println(profileStoreReady ? "YES" : "NO");
}

}  // namespace TurnHubWebApi
