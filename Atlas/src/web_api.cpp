#include "web_api.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "firmware_version.h"
#include "sigil_bus.h"
#include "turnhub_types.h"

namespace TurnHubWebApi {
namespace {

using TurnHub::INVALID_ID;
using TurnHub::MAX_PHYSICAL_SIGILS;
using TurnHub::MAX_PLAYERS;
using TurnHub::SigilBus;
using TurnHub::SigilRecord;

constexpr uint32_t CLAIM_TIMEOUT_MS = 30000;
constexpr uint32_t SESSION_TIMEOUT_MS = 8UL * 60UL * 60UL * 1000UL;
constexpr uint8_t MAX_PENDING_CLAIMS = 6;
constexpr uint8_t MAX_WEB_SESSIONS = MAX_PLAYERS;
constexpr char WIFI_PREF_NAMESPACE[] = "atlas-net";
constexpr char WIFI_PREF_KEY[] = "ap-pass";

struct PendingClaim {
  bool used = false;
  uint64_t requestId = 0;
  uint8_t moduleId = INVALID_ID;
  uint8_t slot = 1;
  uint32_t createdMs = 0;
  bool approved = false;
  char token[33] = {};
};

struct WebSession {
  bool used = false;
  uint8_t moduleId = INVALID_ID;
  uint8_t slot = 1;
  char token[33] = {};
  uint32_t lastSeenMs = 0;
};

PendingClaim pendingClaims[MAX_PENDING_CLAIMS];
WebSession sessions[MAX_WEB_SESSIONS];
WebServer *webServer = nullptr;
ResolveSeatCallback resolveSeat = nullptr;
ControlCallback controlHandler = nullptr;
Preferences preferences;
bool preferencesReady = false;

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

bool resolveSeatNow(uint8_t moduleId, uint8_t slot, SeatSnapshot &snapshot) {
  snapshot = SeatSnapshot{};
  if (resolveSeat == nullptr || !validSlot(slot)) {
    return false;
  }
  return resolveSeat(moduleId, slot, snapshot) && snapshot.exists;
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

String profileKey(char prefix, const uint8_t mac[6], uint8_t slot) {
  char key[16];
  snprintf(
      key,
      sizeof(key),
      "%c%02X%02X%02X%02X%02X%02X%c",
      prefix,
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
      slot == 1 ? 'A' : 'B');
  return String(key);
}

String deviceNameKey(const uint8_t mac[6]) {
  char key[14];
  snprintf(
      key,
      sizeof(key),
      "d%02X%02X%02X%02X%02X%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(key);
}

String profileName(uint8_t moduleId, uint8_t slot) {
  if (!preferencesReady) {
    return String();
  }
  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record = bus != nullptr ? bus->record(moduleId) : nullptr;
  if (record == nullptr) {
    return String();
  }
  const String key = profileKey('n', record->mac, slot);
  return preferences.getString(key.c_str(), "");
}

String customDeviceName(uint8_t moduleId) {
  if (!preferencesReady) {
    return String();
  }
  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record = bus != nullptr ? bus->record(moduleId) : nullptr;
  if (record == nullptr) {
    return String();
  }
  const String key = deviceNameKey(record->mac);
  return preferences.getString(key.c_str(), "");
}

String deviceLabel(uint8_t moduleId) {
  String label = customDeviceName(moduleId);
  if (label.length() == 0) {
    label = "Sigil ";
    label += String(moduleId + 1);
  }
  return label;
}

String storedPinHash(uint8_t moduleId, uint8_t slot) {
  if (!preferencesReady) {
    return String();
  }
  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record = bus != nullptr ? bus->record(moduleId) : nullptr;
  if (record == nullptr) {
    return String();
  }
  const String key = profileKey('p', record->mac, slot);
  return preferences.getString(key.c_str(), "");
}

bool hasPin(uint8_t moduleId, uint8_t slot) {
  return storedPinHash(moduleId, slot).length() == 64;
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

String pinHash(uint8_t moduleId, uint8_t slot, const String &pin) {
  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record = bus != nullptr ? bus->record(moduleId) : nullptr;
  if (record == nullptr) {
    return String();
  }

  String material = sigilHardwareId(record->mac);
  material += ':';
  material += String(slot);
  material += ':';
  material += pin;

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

bool pinMatches(uint8_t moduleId, uint8_t slot, const String &pin) {
  const String stored = storedPinHash(moduleId, slot);
  if (stored.length() != 64) {
    return false;
  }
  const String candidate = pinHash(moduleId, slot, pin);
  return candidate.length() == 64 && stored.equalsIgnoreCase(candidate);
}

void cleanup(uint32_t nowMs) {
  for (auto &pending : pendingClaims) {
    if (pending.used && !pending.approved &&
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
      session.lastSeenMs = nowMs;
      return &session;
    }
  }
  return nullptr;
}

WebSession *sessionForRequest(WebServer &server) {
  return sessionForToken(server.header("X-TurnHub-Token"), millis());
}

WebSession *createSession(uint8_t moduleId, uint8_t slot, uint32_t nowMs) {
  WebSession *target = nullptr;

  for (auto &session : sessions) {
    if (session.used &&
        session.moduleId == moduleId &&
        session.slot == slot) {
      target = &session;
      break;
    }
  }

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
  target->moduleId = moduleId;
  target->slot = slot;
  target->lastSeenMs = nowMs;
  makeToken(target->token);
  return target;
}

bool seatHasSession(uint8_t moduleId, uint8_t slot, uint32_t nowMs) {
  cleanup(nowMs);
  for (const auto &session : sessions) {
    if (session.used && session.moduleId == moduleId && session.slot == slot) {
      return true;
    }
  }
  return false;
}

uint8_t moduleSessionCount(uint8_t moduleId, uint32_t nowMs) {
  cleanup(nowMs);
  uint8_t count = 0;
  for (const auto &session : sessions) {
    if (session.used && session.moduleId == moduleId) {
      ++count;
    }
  }
  return count;
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
      json += '}';
    }
  }

  json += "]}";
  sendJson(server, 200, json);
}

void handleDeviceName(WebServer &server) {
  if (!requireMasterButton(server)) {
    return;
  }
  if (!preferencesReady || !server.hasArg("module") || !server.hasArg("name")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Module and name are required\"}");
    return;
  }

  const int module = server.arg("module").toInt();
  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record =
      bus != nullptr && module >= 0 && module < MAX_PHYSICAL_SIGILS
      ? bus->record(static_cast<uint8_t>(module))
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

  const String key = deviceNameKey(record->mac);
  if (name.length() == 0) {
    preferences.remove(key.c_str());
  } else if (preferences.putString(key.c_str(), name) == 0) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save Sigil name\"}");
    return;
  }

  const String label = name.length() == 0
      ? String("Sigil ") + String(module + 1)
      : name;

  Serial.print("ATLAS|SIGIL|NAME|");
  Serial.print(module);
  Serial.print("|");
  Serial.println(label);

  sendJson(
      server,
      200,
      String("{\"ok\":true,\"label\":\"") + jsonEscape(label) + "\"}");
}

void handleNetworkInfo(WebServer &server) {
  Preferences networkPrefs;
  String password;
  if (networkPrefs.begin(WIFI_PREF_NAMESPACE, true)) {
    password = networkPrefs.getString(WIFI_PREF_KEY, "");
    networkPrefs.end();
  }

  String response = "{\"ssid\":\"";
  response += jsonEscape(String(AtlasConfig::WIFI_SSID));
  response += "\",\"security\":\"WPA2-PSK\",\"passwordConfigured\":";
  response += password.length() >= 8 ? "true" : "false";
  response += ",\"passwordLength\":";
  response += String(password.length());
  response += ",\"stations\":";
  response += String(WiFi.softAPgetStationNum());
  response += ",\"masterButton\":";
  response += masterButtonPressed() ? "true" : "false";
  response += '}';
  sendJson(server, 200, response);
}

void handleNetworkPassword(WebServer &server) {
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

  Preferences networkPrefs;
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

  Serial.println("ATLAS|WIFI_AP|PASSWORD_STORE|UPDATED_FROM_PORTAL");
  sendJson(
      server,
      200,
      "{\"ok\":true,\"changed\":true,\"restarting\":true,\"message\":\"Password saved. Atlas is restarting.\"}");

  // This is an intentional administrative restart. Give the HTTP response a
  // moment to leave the socket before rebooting onto the new WPA2 credential.
  delay(450);
  ESP.restart();
}

void handleSeats(WebServer &server) {
  const uint32_t nowMs = millis();
  String json = "{\"seats\":[";
  json.reserve(3600);
  bool first = true;

  for (uint8_t moduleId = 0; moduleId < MAX_PHYSICAL_SIGILS; ++moduleId) {
    for (uint8_t slot = 1; slot <= 2; ++slot) {
      SeatSnapshot snapshot;
      if (!resolveSeatNow(moduleId, slot, snapshot)) {
        continue;
      }

      if (!first) {
        json += ',';
      }
      first = false;

      const String savedName = profileName(moduleId, slot);
      json += "{\"module\":";
      json += String(moduleId);
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
      json += ",\"name\":\"";
      json += jsonEscape(savedName);
      json += "\",\"hasPin\":";
      json += hasPin(moduleId, slot) ? "true" : "false";
      json += ",\"sessionClaimed\":";
      json += seatHasSession(moduleId, slot, nowMs) ? "true" : "false";
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
  SeatSnapshot snapshot;
  if (module < 0 || module >= MAX_PHYSICAL_SIGILS || !validSlot(slot) ||
      !bus->isOnline(static_cast<uint8_t>(module), millis()) ||
      !resolveSeatNow(static_cast<uint8_t>(module), static_cast<uint8_t>(slot), snapshot)) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Seat is not available\"}");
    return;
  }

  const uint32_t nowMs = millis();
  cleanup(nowMs);

  for (auto &pending : pendingClaims) {
    if (pending.used &&
        pending.moduleId == static_cast<uint8_t>(module) &&
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
  target->moduleId = static_cast<uint8_t>(module);
  target->slot = static_cast<uint8_t>(slot);
  target->createdMs = nowMs;

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

    String response = "{\"ok\":true,\"status\":\"approved\",\"module\":";
    response += String(pending.moduleId);
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

  if (!pinMatches(
          static_cast<uint8_t>(module),
          static_cast<uint8_t>(slot),
          server.arg("pin"))) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Incorrect PIN\"}");
    return;
  }

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
  response += '}';
  sendJson(server, 200, response);
}

void handleSessionMe(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"authenticated\":false}");
    return;
  }

  SeatSnapshot snapshot;
  if (!resolveSeatNow(session->moduleId, session->slot, snapshot)) {
    *session = WebSession{};
    sendJson(server, 409, "{\"ok\":false,\"authenticated\":false,\"error\":\"Seat is no longer at the table\"}");
    return;
  }

  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record = bus != nullptr ? bus->record(session->moduleId) : nullptr;
  const String savedName = profileName(session->moduleId, session->slot);

  String response = "{\"ok\":true,\"authenticated\":true,\"module\":";
  response += String(session->moduleId);
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
  response += ",\"name\":\"";
  response += jsonEscape(savedName);
  response += "\",\"hasPin\":";
  response += hasPin(session->moduleId, session->slot) ? "true" : "false";
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

  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record = bus != nullptr ? bus->record(session->moduleId) : nullptr;
  if (!preferencesReady || record == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Profile storage unavailable\"}");
    return;
  }

  bool displayProfileChanged = false;

  if (server.hasArg("name")) {
    String name = server.arg("name");
    name.trim();
    if (name.length() > 32) {
      name.remove(32);
    }
    const String key = profileKey('n', record->mac, session->slot);
    if (name.length() == 0) {
      preferences.remove(key.c_str());
    } else {
      preferences.putString(key.c_str(), name);
    }
    displayProfileChanged = true;
  }

  if (server.hasArg("pin")) {
    const String pin = server.arg("pin");
    if (!validPin(pin)) {
      sendJson(server, 400, "{\"ok\":false,\"error\":\"PIN must be 4 to 8 digits\"}");
      return;
    }
    const String hash = pinHash(session->moduleId, session->slot, pin);
    if (hash.length() != 64) {
      sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not hash PIN\"}");
      return;
    }
    const String key = profileKey('p', record->mac, session->slot);
    preferences.putString(key.c_str(), hash);
  }

  if (server.hasArg("clearPin") && server.arg("clearPin") == "1") {
    const String key = profileKey('p', record->mac, session->slot);
    preferences.remove(key.c_str());
  }

  if (
      displayProfileChanged &&
      record->helloInfoValid &&
      (record->capabilities & TurnHubProtocol::CAPABILITY_DISPLAY_PROFILE) != 0) {
    bus->send(
        session->moduleId,
        TurnHubProtocol::PacketType::DisplayProfileRequest);
  }

  sendJson(server, 200, "{\"ok\":true}");
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

void runControl(WebServer &server, WebControl control) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return;
  }

  SeatSnapshot snapshot;
  if (!resolveSeatNow(session->moduleId, session->slot, snapshot)) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"Seat is no longer at the table\"}");
    return;
  }

  if (controlHandler == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Web controls are not configured\"}");
    return;
  }

  String message;
  if (!controlHandler(session->moduleId, session->slot, control, message)) {
    if (message.length() == 0) {
      message = "Control is not available right now";
    }
    sendJson(
        server,
        409,
        String("{\"ok\":false,\"error\":\"") + jsonEscape(message) + "\"}");
    return;
  }

  if (message.length() == 0) {
    message = "Control accepted";
  }
  sendJson(
      server,
      200,
      String("{\"ok\":true,\"message\":\"") + jsonEscape(message) + "\"}");
}

}  // namespace

void configure(
    ResolveSeatCallback resolveSeatCallback,
    ControlCallback controlCallback) {
  resolveSeat = resolveSeatCallback;
  controlHandler = controlCallback;
}

void notePhysicalAction(uint8_t sigilId) {
  const uint32_t nowMs = millis();
  cleanup(nowMs);

  PendingClaim *oldest = nullptr;
  for (auto &pending : pendingClaims) {
    if (!pending.used || pending.approved || pending.moduleId != sigilId) {
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
  if (!resolveSeatNow(oldest->moduleId, oldest->slot, snapshot)) {
    *oldest = PendingClaim{};
    return;
  }

  WebSession *session = createSession(oldest->moduleId, oldest->slot, nowMs);
  if (session == nullptr) {
    return;
  }

  oldest->approved = true;
  strncpy(oldest->token, session->token, sizeof(oldest->token) - 1);
  oldest->token[sizeof(oldest->token) - 1] = '\0';

  Serial.print("ATLAS|WEB_SESSION|AUTHORIZED|SIGIL|");
  Serial.print(oldest->moduleId);
  Serial.print("|SLOT|");
  Serial.println(oldest->slot == 1 ? 'A' : 'B');
}

void begin(WebServer &server) {
  if (webServer != nullptr) {
    return;
  }
  webServer = &server;
  preferencesReady = preferences.begin("turnhub", false);

  static const char *headerKeys[] = {"X-TurnHub-Token"};
  server.collectHeaders(headerKeys, 1);

  server.on("/api/devices", HTTP_GET, [&server]() { handleDevices(server); });
  server.on("/api/device/name", HTTP_POST, [&server]() { handleDeviceName(server); });
  server.on("/api/network", HTTP_GET, [&server]() { handleNetworkInfo(server); });
  server.on("/api/network/password", HTTP_POST, [&server]() { handleNetworkPassword(server); });
  server.on("/api/seats", HTTP_GET, [&server]() { handleSeats(server); });
  server.on("/api/session/request", HTTP_POST, [&server]() { handleSessionRequest(server); });
  server.on("/api/session/poll", HTTP_GET, [&server]() { handleSessionPoll(server); });
  server.on("/api/session/login", HTTP_POST, [&server]() { handleSessionLogin(server); });
  server.on("/api/session/me", HTTP_GET, [&server]() { handleSessionMe(server); });
  server.on("/api/session/profile", HTTP_POST, [&server]() { handleProfile(server); });
  server.on("/api/session/logout", HTTP_POST, [&server]() { handleLogout(server); });

  server.on("/api/control/pass", HTTP_POST, [&server]() { runControl(server, WebControl::Pass); });
  server.on("/api/control/pause", HTTP_POST, [&server]() { runControl(server, WebControl::PauseResume); });
  server.on("/api/control/concede", HTTP_POST, [&server]() { runControl(server, WebControl::Concede); });
  server.on("/api/control/win", HTTP_POST, [&server]() { runControl(server, WebControl::ClaimWin); });
  server.on("/api/control/confirm", HTTP_POST, [&server]() { runControl(server, WebControl::ConfirmWin); });
  server.on("/api/control/deny", HTTP_POST, [&server]() { runControl(server, WebControl::DenyWin); });
  server.on("/api/control/starter", HTTP_POST, [&server]() { runControl(server, WebControl::SelectStarter); });

  Serial.print("ATLAS|WEB_API|READY|PROFILES|");
  Serial.println(preferencesReady ? "YES" : "NO");
}

}  // namespace TurnHubWebApi
