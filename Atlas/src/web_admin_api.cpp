// Administration endpoints: paired devices and Sigil naming, the Wi-Fi AP
// password, the downloadable serial log, and account administration. System
// settings additionally need the Admin verified at the table (presence code).

#include <WiFi.h>
#include "sigil_update_service.h"

#include "account_access.h"
#include "config.h"
#include "firmware_version.h"
#include "optional_preferences.h"
#include "pairing_settings.h"
#include "setup_stage.h"
#include "speaker_settings.h"
#include "wifi_password_store.h"
#include "serial_log.h"
#include "web_api_internal.h"
#include "avatars.h"
#include "sd_card.h"

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
  resolveAllSessions(nowMs);

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
    // Both display variants support shared seating. The display bit still selects OTA firmware.
    const bool oled = (record->capabilities & TurnHubProtocol::CAPABILITY_DISPLAY_OLED) != 0;
    json += ",\"display\":\""; json += !record->helloInfoValid ? "unknown" : oled ? "oled" : "epaper";
    json += "\",\"maxPlayers\":"; json += String(2);
    json += ",\"sessionCount\":"; json += String(moduleSessionCount(id));
    json += ",\"profileA\":\""; json += jsonEscape(profileA);
    json += "\"";
    json += "}";
  }
  // Sigils waiting for the owner's pairing-code check (SECURE_LINK.md). The
  // code only proves both ends agreed the same key; it is not a secret.
  json += "],\"pendingPairings\":[";
  first = true;
  for (uint8_t id = 0; bus != nullptr && id < MAX_PHYSICAL_SIGILS; ++id) {
    const TurnHubSecureLink::PendingPairing *pending = bus->pendingPairing(id);
    if (pending == nullptr) continue;
    if (!first) json += ',';
    first = false;
    char code[5];
    TurnHubSecureLink::formatPairingCode(pending->code, code);
    const uint32_t elapsed = nowMs - pending->startedMs;
    const uint32_t leftS = elapsed >= TurnHubSecureLink::PAIR_CONFIRM_TIMEOUT_MS ? 0 :
        (TurnHubSecureLink::PAIR_CONFIRM_TIMEOUT_MS - elapsed + 999) / 1000;
    json += "{\"id\":"; json += String(id);
    json += ",\"code\":\""; json += code;
    json += "\",\"secondsLeft\":"; json += String(leftS);
    json += "}";
  }
  json += "]}";
  sendJson(server, 200, json);
}

void handleDeviceName(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requirePhysicalPresence(server)) return;
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

// Forgets one Sigil (module=<id>) or every Sigil (all=1). Admin only; the
// pairing trust store is table state, so Atlas decides through an Intent.
void handleForgetDevice(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const bool all = server.arg("all") == "1";
  if (!all && !server.hasArg("module")) {
    sendError(server, 400, "Choose a Sigil or all Sigils");
    return;
  }
  const int32_t target = all ? TurnHub::FORGET_ALL_SIGILS : server.arg("module").toInt();
  String message = "Device management unavailable";
  if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId,
          TurnHub::IntentType::ForgetPairing, target, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

void handlePairingSettings(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  String json = "{\"windowMs\":";
  json += String(readPairingWindow ? readPairingWindow() : TurnHub::DEFAULT_PAIRING_WINDOW_MS);
  json += ",\"sigilWindowMs\":";
  json += String(TurnHubProtocol::PAIRING_WINDOW_MS);
  json += ",\"choicesMs\":[";
  bool first = true;
  for (uint8_t seconds : TurnHub::PAIRING_WINDOW_CHOICES_S) {
    if (!first) json += ',';
    first = false;
    json += String(seconds * 1000UL);
  }
  json += "]}";
  sendJson(server, 200, json);
}

void handleSavePairingSettings(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!server.hasArg("windowMs")) {
    sendError(server, 400, "windowMs is required");
    return;
  }
  String message = "Device management unavailable";
  if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId,
          TurnHub::IntentType::ConfigurePairing, server.arg("windowMs").toInt(), message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// Atlas speaker volume. Admin only; saving is an Intent like the pairing window.
void handleSpeakerSettings(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const uint8_t volume = readSpeakerVolume ? readSpeakerVolume() : TurnHub::DEFAULT_SPEAKER_VOLUME;
  String json = "{\"volume\":";
  json += String(volume);
  json += ",\"name\":\"";
  json += TurnHub::speakerVolumeName(volume);
  json += "\",\"max\":";
  json += String(TurnHub::SPEAKER_VOLUME_MAX);
  json += '}';
  sendJson(server, 200, json);
}

void handleSaveSpeakerSettings(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const String volume = server.arg("volume");
  if (volume.length() != 1 || volume[0] < '0' || volume[0] > '9') {
    sendError(server, 400, "volume must be 0 (off) to 3 (high)");
    return;
  }
  String message = "Device management unavailable";
  if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId,
          TurnHub::IntentType::ConfigureSpeaker, server.arg("volume").toInt(), message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// Returns the table to an empty lobby (ResetTable). Admin, and verified
// at the table; Atlas re-checks both in the Intent handler.
void handleResetTable(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requirePhysicalPresence(server)) return;
  String message = "Device management unavailable";
  if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId,
          TurnHub::IntentType::ResetTable, 0, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// Pairing v2 code check from the portal: an Admin verified at the table says
// whether the Sigil shows the same code as Atlas (PairConfirm Intent).
void handlePairConfirm(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requirePhysicalPresence(server)) return;
  if (!server.hasArg("module")) {
    sendError(server, 400, "Choose the waiting Sigil");
    return;
  }
  const int32_t slot = server.arg("module").toInt();
  const bool accept = server.arg("accept") == "1";
  String message = "Device management unavailable";
  if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId,
          TurnHub::IntentType::PairConfirm, slot | (accept ? TurnHub::PAIR_CONFIRM_ACCEPT : 0),
          message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// Factory reset: atlas=1 for Atlas itself, or module=<id> for one Sigil.
// Admin, and verified at the table; Atlas re-checks both, and
// that no match is running, in the FactoryReset Intent handler.
void handleFactoryReset(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requirePhysicalPresence(server)) return;
  const bool atlas = server.arg("atlas") == "1";
  if (!atlas && !server.hasArg("module")) {
    sendError(server, 400, "Choose Atlas or a Sigil");
    return;
  }
  const int32_t target = atlas ? TurnHub::FACTORY_RESET_ATLAS : server.arg("module").toInt();
  String message = "Device management unavailable";
  if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId,
          TurnHub::IntentType::FactoryReset, target, message)) {
    sendError(server, 409, message);
    return;
  }
  sendOkMessage(server, message);
}

// --- Network ------------------------------------------------------------------------------

void handleNetworkInfo(WebServer &server) {
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  const String password = TurnHub::readStoredWifiPassword();
  // No owner-set password means Atlas is running on the shipped default.
  const bool ownerSet = TurnHub::validWifiPassword(password) &&
      password != AtlasConfig::WIFI_DEFAULT_PASSWORD;
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
  response += ",\"verifiedAtTable\":";
  response += jsonBool(physicalPresence(server));
  response += '}';
  sendJson(server, 200, response);
}

namespace {

// Stores an owner-chosen AP password (used from the next start-up). Sends the
// error and returns false if it is invalid or could not be saved.
bool storeWifiPassword(WebServer &server, const String &password, bool &changed) {
  if (!TurnHub::validWifiPassword(password)) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Wi-Fi password must be 8 to 63 characters\"}");
    return false;
  }
  // The shipped default is never stored: a stored password always means the
  // owner chose it (main.cpp loadWifiPassword).
  if (password == AtlasConfig::WIFI_DEFAULT_PASSWORD) {
    sendError(server, 400, "Choose a password other than the one printed for setup");
    return false;
  }
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(AtlasConfig::WIFI_PREF_NAMESPACE, false)) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Network settings storage unavailable\"}");
    return false;
  }
  changed = prefs.getString(AtlasConfig::WIFI_PREF_KEY, "") != password;
  const size_t written = changed ? prefs.putString(AtlasConfig::WIFI_PREF_KEY, password) : password.length();
  prefs.end();
  if (written == 0) {
    sendJson(server, 500, "{\"ok\":false,\"error\":\"Could not save Wi-Fi password\"}");
    return false;
  }
  if (changed) serialLog.println("ATLAS|WIFI_AP|PASSWORD_STORE|UPDATED_FROM_PORTAL");
  return true;
}

void restartSoon() {
  delay(450);  // Let the response reach the phone before the AP drops.
  ESP.restart();
}

}  // namespace

// Saves a new AP password and restarts Atlas so the AP uses it.
void handleNetworkPassword(WebServer &server) {
  if (TurnHubAtlas::sigilUpdatesBusy()) { sendError(server, 409, "Wait for the firmware update to finish"); return; }
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requirePhysicalPresence(server)) return;
  if (!server.hasArg("password")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"New password is required\"}");
    return;
  }
  bool changed = false;
  if (!storeWifiPassword(server, server.arg("password"), changed)) return;
  if (!changed) {
    sendJson(server, 200, "{\"ok\":true,\"changed\":false,\"message\":\"Wi-Fi password is already set to that value\"}");
    return;
  }
  sendJson(server, 200,
      "{\"ok\":true,\"changed\":true,\"restarting\":true,\"message\":\"Password saved. Atlas is restarting.\"}");
  restartSoon();
}

// --- First-run setup (FIRST_RUN_SETUP.md) --------------------------------------------------
// One flow for the Android app and the portal: account, table code (which
// makes the first Admin through /api/accounts/setup), Wi-Fi password, finish.

// No sign-in: a phone that has just joined needs to know where to start.
// Never includes the password.
void handleSetupStatus(WebServer &server) {
  const uint8_t stage = readSetupStage ? readSetupStage() : static_cast<uint8_t>(TurnHub::SetupStage::Complete);
  String primary;
  const bool adminExists = TurnHubAccounts::primaryAdmin(primary) && primary.length() > 0;
  const String stored = TurnHub::readStoredWifiPassword();
  const bool passwordIsDefault = !TurnHub::validWifiPassword(stored) || stored == AtlasConfig::WIFI_DEFAULT_PASSWORD;
  String json = "{\"stage\":\"";
  json += TurnHub::setupStageName(static_cast<TurnHub::SetupStage>(stage));
  json += "\",\"adminExists\":";
  json += jsonBool(adminExists);
  json += ",\"passwordIsDefault\":";
  json += jsonBool(passwordIsDefault);
  json += ",\"ssid\":\"";
  json += jsonEscape(String(AtlasConfig::WIFI_SSID));
  json += "\"}";
  sendJson(server, 200, json);
}

// The last step: store the table's own Wi-Fi password, mark setup finished
// (AdvanceSetup, which Atlas validates) and restart onto the new password.
void handleSetupFinish(WebServer &server) {
  if (TurnHubAtlas::sigilUpdatesBusy()) { sendError(server, 409, "Wait for the firmware update to finish"); return; }
  if (!requirePermission(server, TurnHubAccounts::Admin)) return;
  if (!requirePhysicalPresence(server)) return;
  if (readSetupStage && readSetupStage() != static_cast<uint8_t>(TurnHub::SetupStage::Welcome)) {
    sendError(server, 409, "Setup is already finished");
    return;
  }
  const String password = server.arg("password");
  // The validator reads the stored password, so it is written first; if
  // Atlas then refuses, the previous one goes back, so a refused finish
  // never changes the Wi-Fi at the next restart.
  const String previous = TurnHub::readStoredWifiPassword();
  bool changed = false;
  if (!storeWifiPassword(server, password, changed)) return;
  String message = "Setup unavailable";
  if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId, TurnHub::IntentType::AdvanceSetup,
          static_cast<int32_t>(TurnHub::SetupStage::Finished), message)) {
    if (changed) {
      TurnHub::OptionalPreferences prefs;
      if (prefs.begin(AtlasConfig::WIFI_PREF_NAMESPACE, false)) {
        // An empty value reads as "no owner password": the printed default.
        prefs.putString(AtlasConfig::WIFI_PREF_KEY, previous);
        prefs.end();
      }
      serialLog.println("ATLAS|WIFI_AP|PASSWORD_STORE|RESTORED_AFTER_REFUSED_SETUP");
    }
    sendError(server, 409, message);
    return;
  }
  sendJson(server, 200,
      "{\"ok\":true,\"restarting\":true,\"message\":\"Setup finished. Atlas is restarting with the new Wi-Fi password.\"}");
  restartSoon();
}

// --- Newer firmware (update_notice.h) ---------------------------------------------------------

namespace {

void appendRelease(String &json, const char *name, const TurnHub::FirmwareRelease &release) {
  json += '"'; json += name; json += "\":";
  if (!release.known) {
    json += "null";
    return;
  }
  json += '"';
  json += String(release.major); json += '.';
  json += String(release.minor); json += '.';
  json += String(release.patch);
  json += '"';
}

void sendUpdateStatus(WebServer &server) {
  const TurnHub::LatestFirmware *latest = updateNoticeHooks.latest ? updateNoticeHooks.latest() : nullptr;
  String json = "{\"reported\":";
  json += jsonBool(latest != nullptr);
  json += ",\"latest\":{";
  const TurnHub::LatestFirmware none;
  const TurnHub::LatestFirmware &shown = latest ? *latest : none;
  appendRelease(json, "atlas", shown.atlas); json += ',';
  appendRelease(json, "sigilEink", shown.sigilEink); json += ',';
  appendRelease(json, "sigilOled", shown.sigilOled);
  json += "},\"atlasFirmware\":\"";
  json += TurnHubFirmware::VERSION;
  json += "\",\"updatesAvailable\":";
  json += String(updateNoticeHooks.available ? updateNoticeHooks.available() : 0);
  json += '}';
  sendJson(server, 200, json);
}

}  // namespace

void handleUpdateStatus(WebServer &server) { sendUpdateStatus(server); }

// Public, like the feed it repeats. Omitted products stay unknown; a version
// that doesn't parse refuses the whole report.
void handleLatestFirmware(WebServer &server) {
  TurnHub::LatestFirmware latest;
  const struct { const char *field; TurnHub::FirmwareRelease *release; } fields[] = {
      {"atlas", &latest.atlas}, {"sigilEink", &latest.sigilEink}, {"sigilOled", &latest.sigilOled}};
  bool any = false;
  for (const auto &entry : fields) {
    if (!server.hasArg(entry.field)) continue;
    if (!TurnHub::parseFirmwareRelease(server.arg(entry.field).c_str(), *entry.release)) {
      sendError(server, 400, String(entry.field) + " must be a version such as 0.9.3");
      return;
    }
    any = true;
  }
  if (!any) {
    sendError(server, 400, "Report at least one of atlas, sigilEink and sigilOled");
    return;
  }
  if (updateNoticeHooks.note) updateNoticeHooks.note(latest);
  sendUpdateStatus(server);
}

// --- Diagnostics -------------------------------------------------------------------------------

// Recent serial output as a text file, so a table without a USB cable can
// still hand over a log. The ring is RAM-only (see serial_log.h). It streams
// in small chunks straight from the ring: building the file as one String
// used to need about 32 KB of free heap at once.
void handleSerialLogDownload(WebServer &server) {
  server.sendHeader("Cache-Control", "no-store");
  if (!requirePermission(server, TurnHubAccounts::Developer)) return;
  const String atlasId = atlasHardwareId();
  String header = "# TurnHub Atlas serial log\n# atlasId=";
  header += atlasId;
  header += " bootId=";
  header += bootId;
  header += " firmware=";
  header += TurnHubFirmware::VERSION;
  header += " uptimeMs=";
  header += String(millis());
  header += "\n# Lines are stamped with Atlas uptime in seconds. RAM only: cleared on reboot.\n";
  const uint32_t dropped = serialLog.droppedBytes();
  if (dropped > 0) {
    header += "# Earlier output dropped: ";
    header += String(dropped);
    header += " bytes did not fit in the ";
    header += String(static_cast<uint32_t>(TurnHub::SerialLog::CAPACITY));
    header += "-byte buffer. A microSD card keeps the full log (turnhub/diagnostics.log).\n";
  }
  server.sendHeader("Content-Disposition",
      String("attachment; filename=\"turnhub-") + atlasId + "-" + String(bootId).substring(0, 8) + ".log\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  server.sendContent(header);
  char chunk[512];
  uint64_t cursor = serialLog.firstLineCursor(), lost = 0;
  size_t sent = 0, count;
  // Lines logged while this runs may follow; the bound keeps it finite.
  while (sent < TurnHub::SerialLog::CAPACITY &&
         (count = serialLog.readSince(cursor, chunk, sizeof(chunk), lost)) > 0) {
    server.sendContent(chunk, count);
    sent += count;
  }
  server.sendContent("");
}

// --- Accounts ------------------------------------------------------------------------------------

// GET reports whether first-run Admin setup is still needed. POST makes the
// signed-in account the initial Admin (verified at the table).
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
  if (!requirePhysicalPresence(server)) return;
  if (!TurnHubAccounts::establishAdmin(String(session->profileId))) {
    sendError(server, 503, "Could not establish Admin; a saved PIN is required");
    return;
  }
  sendJson(server, 200, "{\"ok\":true}");
}

// --- Table presence ----------------------------------------------------------------------
// A phone proves its user is at the table by entering the code the Atlas
// screen shows. Admins may ask for a code at any time; before any Admin
// exists, any signed-in account may, to set up the first Admin.

namespace {

bool setupPending() {
  String primary;
  return TurnHubAccounts::primaryAdmin(primary) && primary.length() == 0;
}

// The request's signed-in profile, or sends 401 and returns "".
String presenceProfile(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return String();
  }
  return sessionProfileId(*session);
}

}  // namespace

void handlePresenceStatus(WebServer &server) {
  const String profile = presenceProfile(server);
  if (!profile.length()) return;
  const bool setup = setupPending();
  const uint32_t remaining = presenceHooks.remainingMs ? presenceHooks.remainingMs(profile) : 0;
  String json = "{\"verified\":";
  json += jsonBool(remaining > 0);
  json += ",\"remainingMs\":" + String(remaining);
  json += String(",\"setup\":") + jsonBool(setup);
  json += String(",\"canRequest\":") + jsonBool(setup || TurnHubAccounts::has(profile, TurnHubAccounts::Admin));
  json += '}';
  sendJson(server, 200, json);
}

void handlePresenceRequest(WebServer &server) {
  const String profile = presenceProfile(server);
  if (!profile.length()) return;
  const bool setup = setupPending();
  if (!setup && !TurnHubAccounts::has(profile, TurnHubAccounts::Admin)) {
    sendError(server, 403, "Only an Admin can verify at the table");
    return;
  }
  if (!presenceHooks.request || !presenceHooks.request(profile, setup)) {
    sendError(server, 503, "Atlas could not show a code");
    return;
  }
  sendOkMessage(server, "Enter the code the Atlas screen shows");
}

void handlePresenceConfirm(WebServer &server) {
  const String profile = presenceProfile(server);
  if (!profile.length()) return;
  String digits;
  for (const char c : server.arg("code")) if (c >= '0' && c <= '9') digits += c;
  if (digits.length() != 6 || !presenceHooks.confirm) {
    sendError(server, 400, "Enter the six-digit code from the Atlas screen");
    return;
  }
  switch (presenceHooks.confirm(profile, static_cast<uint32_t>(digits.toInt()))) {
    case PresenceResult::Verified: {
      const uint32_t remaining = presenceHooks.remainingMs ? presenceHooks.remainingMs(profile) : 0;
      sendJson(server, 200, String("{\"ok\":true,\"message\":\"Verified at the table\",\"remainingMs\":") +
          String(remaining) + "}");
      return;
    }
    case PresenceResult::WrongCode:
      sendError(server, 400, "That is not the code on the Atlas screen");
      return;
    case PresenceResult::TooManyAttempts:
      sendError(server, 429, "Too many wrong codes; request a new one");
      return;
    case PresenceResult::NoCode:
      break;
  }
  sendError(server, 409, "No code is showing for you; request a new one");
}

void handlePresenceLock(WebServer &server) {
  const String profile = presenceProfile(server);
  if (!profile.length()) return;
  if (presenceHooks.revoke) presenceHooks.revoke(profile);
  sendOkMessage(server, "No longer verified at the table");
}

// Admins and Game Masters see every account; others see only their own.
// Moderation counts are not listed here: they are private statistics served
// only to their owner (see /api/session/stats).
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
    const uint8_t avatar = TurnHubProfiles::avatarForProfile(id);
    json += ",\"avatar\":";
    json += String(TurnHubAvatars::validPresetAvatar(avatar) ? avatar : 0);
    if (gameMaster || self) {
      json += ",\"nudgeMuted\":";
      json += jsonBool(account.nudgeMuted);
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
namespace {
bool servePackCopy(WebServer &server, const char *packFile) {
  return packFile != nullptr && !server.hasArg("classic") &&
         TurnHubAtlas::sdServePortalFile(server, packFile, "no-store");
}
}  // namespace

void servePortalPage(WebServer &server, const char *packFile, const char *html) {
  if (servePackCopy(server, packFile)) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", html);
}

void serveRestrictedPage(WebServer &server, const char *html, uint8_t permission, const char *packFile) {
  if (!server.header(internal::TOKEN_HEADER).length()) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><p id="m">Checking account access…</p><a href="/portal">Back to portal</a><script>fetch(location.pathname+location.search,{headers:{'X-TurnHub-Token':localStorage.getItem('turnhubSessionToken')||''}}).then(async r=>{if(!r.ok)throw Error('Access denied. Sign in with the required account permission.');const t=await r.text();if(!localStorage.getItem('turnhubSessionToken'))throw Error('Sign in first.');document.open();document.write(t);document.close()}).catch(e=>document.getElementById('m').textContent=e.message)</script>)HTML");
    return;
  }
  if (!requirePermission(server, permission)) return;
  if (servePackCopy(server, packFile)) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", html);
}

}  // namespace TurnHubWebApi
