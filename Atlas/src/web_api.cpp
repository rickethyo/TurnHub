#include "web_api.h"

#include <WiFi.h>
#include <esp_system.h>
#include <stdlib.h>
#include <string.h>

#include "firmware_version.h"
#include "protocol.h"
#include "sigil_bus.h"
#include "turnhub_types.h"

namespace TurnHubWebApi {
namespace {

using TurnHub::INVALID_ID;
using TurnHub::MAX_PHYSICAL_SIGILS;
using TurnHub::SigilBus;
using TurnHub::SigilRecord;
using TurnHubProtocol::PacketType;

constexpr uint32_t CLAIM_TIMEOUT_MS = 30000;
constexpr uint32_t SESSION_TIMEOUT_MS = 8UL * 60UL * 60UL * 1000UL;
constexpr uint8_t MAX_PENDING_CLAIMS = 4;
constexpr uint8_t MAX_WEB_SESSIONS = MAX_PHYSICAL_SIGILS;

struct PendingClaim {
  bool used = false;
  uint64_t requestId = 0;
  uint8_t sigilId = INVALID_ID;
  uint32_t createdMs = 0;
  bool approved = false;
  char token[33] = {};
};

struct WebSession {
  bool used = false;
  uint8_t sigilId = INVALID_ID;
  char token[33] = {};
  uint32_t lastSeenMs = 0;
};

PendingClaim pendingClaims[MAX_PENDING_CLAIMS];
WebSession sessions[MAX_WEB_SESSIONS];
WebServer *webServer = nullptr;

uint64_t random64() {
  return (static_cast<uint64_t>(esp_random()) << 32) |
      static_cast<uint64_t>(esp_random());
}

void makeToken(char out[33]) {
  const uint32_t a = esp_random();
  const uint32_t b = esp_random();
  const uint32_t c = esp_random();
  const uint32_t d = esp_random();
  snprintf(out, 33, "%08lX%08lX%08lX%08lX",
      static_cast<unsigned long>(a),
      static_cast<unsigned long>(b),
      static_cast<unsigned long>(c),
      static_cast<unsigned long>(d));
}

String requestIdText(uint64_t value) {
  char text[17];
  snprintf(text, sizeof(text), "%08lX%08lX",
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

void sendJson(WebServer &server, int status, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json", body);
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

WebSession *createSession(uint8_t sigilId, uint32_t nowMs) {
  WebSession *target = nullptr;

  // One active browser session per physical Sigil for this first migration
  // pass. A new physical confirmation replaces the older browser session.
  for (auto &session : sessions) {
    if (session.used && session.sigilId == sigilId) {
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
  target->sigilId = sigilId;
  target->lastSeenMs = nowMs;
  makeToken(target->token);
  return target;
}

bool moduleHasSession(uint8_t sigilId, uint32_t nowMs) {
  cleanup(nowMs);
  for (const auto &session : sessions) {
    if (session.used && session.sigilId == sigilId) {
      return true;
    }
  }
  return false;
}

String macText(const uint8_t mac[6]) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

String sigilHardwareId(const uint8_t mac[6]) {
  char text[17];
  snprintf(text, sizeof(text), "THS-%02X%02X%02X%02X%02X%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

String atlasHardwareId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  return String("THA-") + mac;
}

void handleDevices(WebServer &server) {
  SigilBus *bus = SigilBus::activeInstance();
  const uint32_t nowMs = millis();

  String json;
  json.reserve(2600);
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

      char firmware[24];
      if (record->helloInfoValid) {
        snprintf(firmware, sizeof(firmware), "%u.%u.%u",
            static_cast<unsigned>(record->firmwareMajor),
            static_cast<unsigned>(record->firmwareMinor),
            static_cast<unsigned>(record->firmwarePatch));
      } else {
        snprintf(firmware, sizeof(firmware), "unknown");
      }

      json += "{\"id\":";
      json += String(id);
      json += ",\"label\":\"Sigil ";
      json += String(id + 1);
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
      json += ",\"sessionClaimed\":";
      json += moduleHasSession(id, nowMs) ? "true" : "false";
      json += '}';
    }
  }

  json += "]}";
  sendJson(server, 200, json);
}

void handleSessionRequest(WebServer &server) {
  SigilBus *bus = SigilBus::activeInstance();
  if (bus == nullptr || !server.hasArg("module")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Missing Sigil module\"}");
    return;
  }

  const int module = server.arg("module").toInt();
  if (module < 0 || module >= MAX_PHYSICAL_SIGILS ||
      !bus->isOnline(static_cast<uint8_t>(module), millis())) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Sigil is not online\"}");
    return;
  }

  const uint32_t nowMs = millis();
  cleanup(nowMs);

  for (auto &pending : pendingClaims) {
    if (pending.used && pending.sigilId == static_cast<uint8_t>(module)) {
      pending = PendingClaim{};
    }
  }

  PendingClaim *slot = nullptr;
  for (auto &pending : pendingClaims) {
    if (!pending.used) {
      slot = &pending;
      break;
    }
  }

  if (slot == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Too many pending claims\"}");
    return;
  }

  uint64_t requestId = random64();
  if (requestId == 0) {
    requestId = 1;
  }

  *slot = PendingClaim{};
  slot->used = true;
  slot->requestId = requestId;
  slot->sigilId = static_cast<uint8_t>(module);
  slot->createdMs = nowMs;

  String response = "{\"ok\":true,\"status\":\"pending\",\"requestId\":\"";
  response += requestIdText(requestId);
  response += "\",\"module\":";
  response += String(module);
  response += ",\"expiresMs\":";
  response += String(CLAIM_TIMEOUT_MS);
  response += ",\"message\":\"Press Action on this Sigil to authorize the browser session.\"}";
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

  const uint32_t nowMs = millis();
  cleanup(nowMs);

  for (auto &pending : pendingClaims) {
    if (!pending.used || pending.requestId != id) {
      continue;
    }

    if (!pending.approved) {
      sendJson(server, 200, "{\"ok\":true,\"status\":\"pending\"}");
      return;
    }

    String response = "{\"ok\":true,\"status\":\"approved\",\"module\":";
    response += String(pending.sigilId);
    response += ",\"token\":\"";
    response += pending.token;
    response += "\"}";
    pending = PendingClaim{};
    sendJson(server, 200, response);
    return;
  }

  sendJson(server, 404, "{\"ok\":false,\"status\":\"expired\",\"error\":\"Claim expired or was already collected\"}");
}

void handleSessionMe(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"authenticated\":false}");
    return;
  }

  SigilBus *bus = SigilBus::activeInstance();
  const SigilRecord *record = bus != nullptr ? bus->record(session->sigilId) : nullptr;

  String response = "{\"ok\":true,\"authenticated\":true,\"module\":";
  response += String(session->sigilId);
  if (record != nullptr) {
    response += ",\"hardwareId\":\"";
    response += sigilHardwareId(record->mac);
    response += '"';
  }
  response += '}';
  sendJson(server, 200, response);
}

void handleLogout(WebServer &server) {
  const String token = server.header("X-TurnHub-Token");
  for (auto &session : sessions) {
    if (session.used && token.equalsIgnoreCase(session.token)) {
      session = WebSession{};
      sendJson(server, 200, "{\"ok\":true}");
      return;
    }
  }
  sendJson(server, 200, "{\"ok\":true}");
}

bool injectAuthorized(WebServer &server, PacketType type) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return false;
  }

  SigilBus *bus = SigilBus::activeInstance();
  if (bus == nullptr || !bus->isOnline(session->sigilId, millis())) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"Claimed Sigil is offline\"}");
    return false;
  }

  if (!bus->injectEvent(session->sigilId, type)) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Atlas event queue is busy\"}");
    return false;
  }
  return true;
}

void handlePass(WebServer &server) {
  if (injectAuthorized(server, PacketType::Pass)) {
    sendJson(server, 200, "{\"ok\":true,\"control\":\"pass\"}");
  }
}

void handleAction(WebServer &server) {
  if (injectAuthorized(server, PacketType::ActionShort)) {
    sendJson(server, 200, "{\"ok\":true,\"control\":\"action\"}");
  }
}

void handleHold(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return;
  }

  SigilBus *bus = SigilBus::activeInstance();
  if (bus == nullptr || !bus->isOnline(session->sigilId, millis())) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"Claimed Sigil is offline\"}");
    return;
  }

  const bool ok =
      bus->injectEvent(session->sigilId, PacketType::ActionDown) &&
      bus->injectEvent(session->sigilId, PacketType::ActionLong) &&
      bus->injectEvent(session->sigilId, PacketType::ActionUp);

  sendJson(server, ok ? 200 : 503,
      ok ? "{\"ok\":true,\"control\":\"hold\"}"
         : "{\"ok\":false,\"error\":\"Atlas event queue is busy\"}");
}

void handleWin(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Browser session is not authorized\"}");
    return;
  }

  SigilBus *bus = SigilBus::activeInstance();
  if (bus == nullptr || !bus->isOnline(session->sigilId, millis())) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"Claimed Sigil is offline\"}");
    return;
  }

  // Mirrors a physical long hold through the victory threshold. The existing
  // game engine still decides whether the claimed Sigil is allowed to win.
  const bool ok =
      bus->injectEvent(session->sigilId, PacketType::ActionDown) &&
      bus->injectEvent(session->sigilId, PacketType::ActionLong) &&
      bus->injectEvent(session->sigilId, PacketType::ActionWin) &&
      bus->injectEvent(session->sigilId, PacketType::ActionUp);

  sendJson(server, ok ? 200 : 503,
      ok ? "{\"ok\":true,\"control\":\"win\"}"
         : "{\"ok\":false,\"error\":\"Atlas event queue is busy\"}");
}

}  // namespace

void notePhysicalAction(uint8_t sigilId) {
  const uint32_t nowMs = millis();
  cleanup(nowMs);

  PendingClaim *oldest = nullptr;
  for (auto &pending : pendingClaims) {
    if (!pending.used || pending.approved || pending.sigilId != sigilId) {
      continue;
    }
    if (oldest == nullptr || pending.createdMs < oldest->createdMs) {
      oldest = &pending;
    }
  }

  if (oldest == nullptr) {
    return;
  }

  WebSession *session = createSession(sigilId, nowMs);
  if (session == nullptr) {
    return;
  }

  oldest->approved = true;
  strncpy(oldest->token, session->token, sizeof(oldest->token) - 1);
  oldest->token[sizeof(oldest->token) - 1] = '\0';

  Serial.print("ATLAS|WEB_SESSION|AUTHORIZED|SIGIL|");
  Serial.println(sigilId);
}

void begin(WebServer &server) {
  if (webServer != nullptr) {
    return;
  }
  webServer = &server;

  static const char *headerKeys[] = {"X-TurnHub-Token"};
  server.collectHeaders(headerKeys, 1);

  server.on("/api/devices", HTTP_GET, [&server]() {
    handleDevices(server);
  });
  server.on("/api/session/request", HTTP_POST, [&server]() {
    handleSessionRequest(server);
  });
  server.on("/api/session/poll", HTTP_GET, [&server]() {
    handleSessionPoll(server);
  });
  server.on("/api/session/me", HTTP_GET, [&server]() {
    handleSessionMe(server);
  });
  server.on("/api/session/logout", HTTP_POST, [&server]() {
    handleLogout(server);
  });
  server.on("/api/control/pass", HTTP_POST, [&server]() {
    handlePass(server);
  });
  server.on("/api/control/action", HTTP_POST, [&server]() {
    handleAction(server);
  });
  server.on("/api/control/hold", HTTP_POST, [&server]() {
    handleHold(server);
  });
  server.on("/api/control/win", HTTP_POST, [&server]() {
    handleWin(server);
  });

  Serial.println("ATLAS|WEB_API|READY");
}

}  // namespace TurnHubWebApi
