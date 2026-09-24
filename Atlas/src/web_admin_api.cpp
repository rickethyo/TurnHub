// Administration endpoints: paired devices and Sigil naming, the Wi-Fi AP
// password, the downloadable serial log, and account administration. System
// settings additionally require the physical master button to be held.

#include <WiFi.h>

#include "account_access.h"
#include "config.h"
#include "firmware_version.h"
#include "optional_preferences.h"
#include "wifi_password_store.h"
#include "serial_log.h"
#include "web_api_internal.h"

using TurnHub::serialLog;

namespace TurnHubWebApi {
namespace internal {

namespace {

String macText(const uint8_t mac[6]) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

String customDeviceName(uint8_t controllerId) {
  const SigilRecord *record = recordForModule(controllerId);
  return record != nullptr ? TurnHubProfiles::deviceName(record->mac) : String();
}

String defaultDeviceLabel(uint8_t controllerId) {
  return String("Sigil ") + String(controllerId + 1);
}

String deviceLabel(uint8_t controllerId) {
  const String label = customDeviceName(controllerId);
  return label.length() ? label : defaultDeviceLabel(controllerId);
}

String firmwareText(const SigilRecord &record) {
  if (!record.helloInfoValid) return String("unknown");
  char firmware[24];
  snprintf(firmware, sizeof(firmware), "%u.%u.%u",
      static_cast<unsigned>(record.firmwareMajor),
      static_cast<unsigned>(record.firmwareMinor),
      static_cast<unsigned>(record.firmwarePatch));
  return String(firmware);
}

// Loads the initial Admin's ID and the target account, or sends 503.
bool loadAdminTarget(WebServer &server, const String &id, String &primary, TurnHubAccounts::Account &account) {
  if (TurnHubAccounts::primaryAdmin(primary) && TurnHubAccounts::load(id, account)) return true;
  sendError(server, 503, "Account unavailable");
  return false;
}

}  // namespace

// --- Identity helpers -------------------------------------------------------------

const SigilRecord *recordForModule(uint8_t controllerId) {
  SigilBus *bus = SigilBus::activeInstance();
  return bus != nullptr ? bus->record(controllerId) : nullptr;
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

// --- Devices --------------------------------------------------------------------------

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
  for (uint8_t id = 0; bus != nullptr && id < MAX_PHYSICAL_SIGILS; ++id) {
    const SigilRecord *record = bus->record(id);
    if (record == nullptr) continue;
    if (!first) json += ',';
    first = false;

    const uint32_t ageMs = nowMs - record->lastSeenMs;
    const String profileA = TurnHubProfiles::boundProfileIdForSeat(record->mac, 1);

    json += "{\"id\":"; json += String(id);
    json += ",\"label\":\""; json += jsonEscape(deviceLabel(id));
    json += "\",\"defaultLabel\":\""; json += defaultDeviceLabel(id);
    json += "\",\"customName\":\""; json += jsonEscape(customDeviceName(id));
    json += "\",\"hardwareId\":\""; json += sigilHardwareId(record->mac);
    json += "\",\"mac\":\""; json += macText(record->mac);
    json += "\",\"online\":"; json += jsonBool(ageMs <= SigilBus::SIGIL_TIMEOUT_MS);
    json += ",\"ageMs\":"; json += String(ageMs);
    json += ",\"firmware\":\""; json += firmwareText(*record);
    json += "\",\"metadata\":"; json += jsonBool(record->helloInfoValid);
    json += ",\"capabilities\":"; json += String(record->capabilities);
    json += ",\"sessionCount\":"; json += String(moduleSessionCount(id, nowMs));
    json += ",\"profileA\":\""; json += jsonEscape(profileA);
    json += "\"}";
  }
  json += "]}";
  sendJson(server, 200, json);
}

void handleDeviceName(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requireMasterButton(server)) return;
  if (!profileStoreReady || !server.hasArg("module") || !server.hasArg("name")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Module and name are required\"}");
    return;
  }

  const int module = server.arg("module").toInt();
  const SigilRecord *record = module >= 0 && module < MAX_PHYSICAL_SIGILS
      ? recordForModule(static_cast<uint8_t>(module))
      : nullptr;
  if (record == nullptr) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Sigil is not known to Atlas\"}");
    return;
  }

  const String name = cleanName(server.arg("name"));
  if (!TurnHubProfiles::setDeviceName(record->mac, name)) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save Sigil name\"}");
    return;
  }

  const String label = name.length() == 0 ? defaultDeviceLabel(static_cast<uint8_t>(module)) : name;
  serialLog.print("ATLAS|SIGIL|NAME|");
  serialLog.print(module);
  serialLog.print("|");
  serialLog.println(label);
  sendJson(server, 200, String("{\"ok\":true,\"label\":\"") + jsonEscape(label) + "\"}");
}

// --- Network ------------------------------------------------------------------------------

void handleNetworkInfo(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const String password = TurnHub::readStoredWifiPassword();
  // No owner-set password means Atlas is running on the shipped default.
  const bool ownerSet = password.length() >= AtlasConfig::WIFI_PASSWORD_MIN_LENGTH;
  String response = "{\"ssid\":\"";
  response += jsonEscape(String(AtlasConfig::WIFI_SSID));
  response += "\",\"security\":\"WPA2-PSK\",\"passwordConfigured\":";
  response += jsonBool(ownerSet);
  response += ",\"passwordIsDefault\":";
  response += jsonBool(!ownerSet);
  response += ",\"passwordLength\":";
  response += String(ownerSet ? password.length() : sizeof(AtlasConfig::WIFI_DEFAULT_PASSWORD) - 1);
  response += ",\"stations\":";
  response += String(WiFi.softAPgetStationNum());
  response += ",\"masterButton\":";
  response += jsonBool(AtlasConfig::masterButtonPressed());
  response += '}';
  sendJson(server, 200, response);
}

// Saves a new AP password and restarts Atlas so the AP uses it.
void handleNetworkPassword(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requireMasterButton(server)) return;
  if (!server.hasArg("password")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"New password is required\"}");
    return;
  }

  const String password = server.arg("password");
  if (!TurnHub::validWifiPassword(password)) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Wi-Fi password must be 8 to 63 characters\"}");
    return;
  }

  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(AtlasConfig::WIFI_PREF_NAMESPACE, false)) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Network settings storage unavailable\"}");
    return;
  }
  if (prefs.getString(AtlasConfig::WIFI_PREF_KEY, "") == password) {
    prefs.end();
    sendJson(server, 200, "{\"ok\":true,\"changed\":false,\"message\":\"Wi-Fi password is already set to that value\"}");
    return;
  }
  const size_t written = prefs.putString(AtlasConfig::WIFI_PREF_KEY, password);
  prefs.end();
  if (written == 0) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save Wi-Fi password\"}");
    return;
  }

  serialLog.println("ATLAS|WIFI_AP|PASSWORD_STORE|UPDATED_FROM_PORTAL");
  sendJson(server, 200,
      "{\"ok\":true,\"changed\":true,\"restarting\":true,\"message\":\"Password saved. Atlas is restarting.\"}");
  delay(450);  // Let the response reach the browser before the AP drops.
  ESP.restart();
}

// --- Diagnostics -------------------------------------------------------------------------------

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
  server.sendHeader("Content-Disposition",
      String("attachment; filename=\"turnhub-") + atlasId + "-" + String(bootId).substring(0, 8) + ".log\"");
  server.send(200, "text/plain; charset=utf-8", body);
}

// --- Accounts ------------------------------------------------------------------------------------

// GET reports whether first-run Admin setup is still needed. POST makes the
// signed-in account the initial Admin (master button required).
void handleAccountSetup(WebServer &server, bool readOnly) {
  String primary;
  if (!TurnHubAccounts::primaryAdmin(primary)) {
    sendError(server, 503, "Account storage unavailable");
    return;
  }
  if (readOnly) {
    sendJson(server, 200, String("{\"setupRequired\":") + jsonBool(primary.length() == 0) + "}");
    return;
  }
  if (primary.length()) {
    sendError(server, 409, "Admin setup is already complete");
    return;
  }
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Create or sign into your account first");
    return;
  }
  if (!requireMasterButton(server)) return;
  if (!TurnHubAccounts::establishAdmin(String(session->profileId))) {
    sendError(server, 503, "Could not establish Admin; a saved PIN is required");
    return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

// Admins and Game Masters see every account; others see only their own.
// Moderation counts are visible to Game Masters and the account itself.
void handleAccounts(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return;
  }
  TurnHubAccounts::Account actor;
  if (!TurnHubAccounts::load(String(session->profileId), actor)) {
    sendError(server, 503, "Account unavailable");
    return;
  }
  const bool admin = actor.permissions & TurnHubAccounts::Admin;
  const bool gameMaster = actor.permissions & TurnHubAccounts::GameMaster;
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][TurnHubProfiles::PROFILE_ID_LENGTH + 1];
  const size_t count = TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES);
  String json = "{\"accounts\":[";
  bool comma = false;
  for (size_t i = 0; i < count; ++i) {
    const String id(ids[i]);
    const bool self = id == session->profileId;
    if (!admin && !gameMaster && !self) continue;
    TurnHubAccounts::Account account;
    if (!TurnHubAccounts::load(id, account)) continue;
    if (account.archived && !admin) continue;
    if (comma) json += ',';
    comma = true;
    json += "{\"profileId\":\"" + id + "\",\"name\":\"" +
        jsonEscape(TurnHubProfiles::nameForProfile(id)) + "\",\"permissions\":" + String(account.permissions);
    json += ",\"archived\":";
    json += jsonBool(account.archived);
    if (gameMaster || self) {
      json += ",\"connectionResets\":" + String(account.connectionResets) +
          ",\"gameRemovals\":" + String(account.gameRemovals) +
          ",\"nudgeMuted\":" + jsonBool(account.nudgeMuted);
    }
    json += '}';
  }
  json += "]}";
  sendJson(server, 200, json);
}

void handleAccountPermissions(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const String id = server.arg("profileId");
  const String raw = server.arg("permissions");
  const int flags = raw.toInt();
  // Moderation abilities are only meaningful on a Game Master.
  if (raw != String(flags) || flags < 0 || flags > TurnHubAccounts::ALL_PERMISSIONS ||
      ((flags & TurnHubAccounts::MODERATION_PERMISSIONS) && !(flags & TurnHubAccounts::GameMaster))) {
    sendError(server, 400, "Invalid permissions");
    return;
  }
  String primary;
  TurnHubAccounts::Account account;
  if (!loadAdminTarget(server, id, primary, account)) return;
  if ((id == primary && !(flags & TurnHubAccounts::Admin)) ||
      (flags && !TurnHubProfiles::hasPinForProfile(id))) {
    sendError(server, 409, "Keep the initial Admin and a PIN on privileged accounts");
    return;
  }
  account.permissions = uint8_t(flags);
  if (!TurnHubAccounts::save(id, account)) {
    sendError(server, 503, "Could not save permissions");
    return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

void handleAccountArchive(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const String id = server.arg("profileId");
  const String value = server.arg("archived");
  if (value != "0" && value != "1") {
    sendError(server, 400, "Choose archive or restore");
    return;
  }
  String primary;
  TurnHubAccounts::Account account;
  if (!loadAdminTarget(server, id, primary, account)) return;
  const bool archive = value == "1";
  if (archive && id == primary) {
    sendError(server, 409, "The initial Admin cannot be archived");
    return;
  }
  uint8_t controller = INVALID_ID, slot = 1;
  if (archive && resolveProfile && resolveProfile(id, controller, slot)) {
    sendError(server, 409,
        "Account is still at the table. Leave the table or reset the completed game before archiving");
    return;
  }
  if (account.archived != archive) {
    account.archived = archive;
    if (!TurnHubAccounts::save(id, account)) {
      sendError(server, 503, "Could not save archive state");
      return;
    }
  }
  if (archive) revokeConnections(id);
  sendJson(server, 200, "{\"ok\":true}");
}

void handleModerate(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::GameMaster)) return;
  const String actor(sessionForRequest(server)->profileId);
  String message;
  if (!moderateHandler ||
      !moderateHandler(actor, server.arg("profileId"), server.arg("action"), message)) {
    sendError(server, 409, message);
    return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

}  // namespace internal

// Serves a permission-gated page. A plain navigation has no token header,
// so it first gets a small loader that re-requests the page with the
// browser's stored session token.
void serveRestrictedPage(WebServer &server, const char *html, uint8_t permission) {
  server.sendHeader("Cache-Control", "no-store");
  if (!server.header(internal::TOKEN_HEADER).length()) {
    server.send(200, "text/html", R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><p id="m">Checking account access…</p><a href="/portal">Back to portal</a><script>fetch(location.pathname,{headers:{'X-TurnHub-Token':localStorage.getItem('turnhubSessionToken')||''}}).then(async r=>{if(!r.ok)throw Error('Access denied. Sign in with the required account permission.');const t=await r.text();if(!localStorage.getItem('turnhubSessionToken'))throw Error('Sign in first.');document.open();document.write(t);document.close()}).catch(e=>document.getElementById('m').textContent=e.message)</script>)HTML");
    return;
  }
  if (requirePermission(server, permission)) server.send_P(200, "text/html", html);
}

}  // namespace TurnHubWebApi
