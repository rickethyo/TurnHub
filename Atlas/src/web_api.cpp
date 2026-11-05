// HTTP API entry point: owns the callbacks Atlas registers, the shared
// response helpers and the route table. Handlers live in the web_*.cpp
// modules listed in web_api_internal.h.

#include "web_api.h"

#include "config.h"
#include "serial_log.h"
#include "http_diagnostics.h"
#include "firmware_version.h"
#include "web_api_internal.h"

using TurnHub::serialLog;

namespace TurnHubWebApi {
namespace internal {

bool profileStoreReady = false;
char bootId[TOKEN_LENGTH + 1] = {};

DeviceIntentCallback deviceHandler = nullptr;
PairingWindowCallback readPairingWindow = nullptr;
PresenceHooks presenceHooks;
UpdateNoticeHooks updateNoticeHooks;
SpeakerVolumeCallback readSpeakerVolume = nullptr;
SetupStageCallback readSetupStage = nullptr;
StateCallback readClientState = nullptr;
RevisionCallback readClientRevision = nullptr;
HardwareTestCallback hardwareTestHandler = nullptr;

namespace {
WebServer *webServer = nullptr;
}  // namespace

void sendJson(WebServer &server, int status, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json", body);
}

void sendError(WebServer &server, int status, const String &message) {
  sendJson(server, status, String("{\"error\":\"") + jsonEscape(message) + "\"}");
}

void sendOkMessage(WebServer &server, const String &message) {
  sendJson(server, 200, String("{\"ok\":true,\"message\":\"") + jsonEscape(message) + "\"}");
}

bool physicalPresence(WebServer &server) {
  const WebSession *session = sessionForRequest(server);
  return session != nullptr && presenceHooks.remainingMs != nullptr &&
      presenceHooks.remainingMs(sessionProfileId(*session)) > 0;
}

bool requirePhysicalPresence(WebServer &server) {
  if (physicalPresence(server)) return true;
  // "presenceRequired" lets the portal start Verify at the table and retry.
  sendJson(server, 403,
      "{\"ok\":false,\"presenceRequired\":true,\"error\":\"Verify at the table first: request a code and enter the one the Atlas screen shows\"}");
  return false;
}

}  // namespace internal

using namespace internal;

// --- Callback registration -------------------------------------------------------

void configureClientState(StateCallback state, RevisionCallback revision) {
  readClientState = state;
  readClientRevision = revision;
}

void configureDevices(DeviceIntentCallback manage, PairingWindowCallback window) {
  deviceHandler = manage;
  readPairingWindow = window;
}

void configureHardwareTests(HardwareTestCallback callback) {
  hardwareTestHandler = callback;
}

void configurePresence(const PresenceHooks &hooks) {
  presenceHooks = hooks;
}

void configureUpdateNotice(const UpdateNoticeHooks &hooks) {
  updateNoticeHooks = hooks;
}

void configureSpeaker(SpeakerVolumeCallback volume) {
  readSpeakerVolume = volume;
}

void configureSetup(SetupStageCallback stage) {
  readSetupStage = stage;
}

// --- Routes ---------------------------------------------------------------------------

namespace {
// Handlers that carry a fixed argument, as plain functions so the route
// table can stay constant data in flash.
void accountSetupStatus(WebServer &server) { handleAccountSetup(server, true); }
void accountSetupCreate(WebServer &server) { handleAccountSetup(server, false); }
struct Route {
  const char *uri;
  HTTPMethod method;
  void (*handler)(WebServer &);
};

// One table, matched by one request handler. A server.on() per route cost
// about 140 bytes of heap each (handler object, URI object, path copy):
// roughly 10 KB for these routes.
const Route ROUTES[] = {
  // Client contract (protocol/http-v1.md).
  {"/api/v1/info", HTTP_GET, handleInfo},
  {"/api/time", HTTP_POST, handleClockSync},
  {"/api/v1/state", HTTP_GET, handleState},
  // Phone-hosted games: the hub runs the game, Atlas shows it (hosted-v1.md).
  // Pair a phone once instead of signing in (phone_links.h).
  {"/api/player-copies", HTTP_POST, handlePlayerCopy},
  {"/api/player-copies", HTTP_GET, handlePlayerCopies},
  {"/api/player-copies/stats", HTTP_GET, handlePlayerCopyStats},
  {"/api/player-copies/history", HTTP_GET, handlePlayerCopyHistory},
  {"/api/phones/pair", HTTP_POST, handlePhonePair},
  {"/api/phones/pair", HTTP_GET, handlePhonePairPoll},
  {"/api/phones/pair/cancel", HTTP_POST, handlePhonePairCancel},
  {"/api/hosted/claim", HTTP_POST, handleHostedClaim},
  {"/api/hosted/release", HTTP_POST, handleHostedRelease},
  {"/api/hosted/state", HTTP_POST, handleHostedPublish},
  {"/api/hosted/state", HTTP_GET, handleHostedState},
  {"/api/hosted/inputs", HTTP_GET, handleHostedInputs},
  {"/api/hosted/room", HTTP_POST, handleControllerRoomPublish},
  {"/api/hosted/room", HTTP_GET, handleControllerRoom},
  {"/api/hosted/controller", HTTP_POST, handleControllerVerify},

  // Accounts and administration.
  {"/api/accounts/setup", HTTP_GET, accountSetupStatus},
  {"/api/accounts/setup", HTTP_POST, accountSetupCreate},
  {"/api/presence", HTTP_GET, handlePresenceStatus},
  {"/api/presence/request", HTTP_POST, handlePresenceRequest},
  {"/api/presence/confirm", HTTP_POST, handlePresenceConfirm},
  {"/api/presence/lock", HTTP_POST, handlePresenceLock},
  {"/api/accounts", HTTP_GET, handleAccounts},
  {"/api/accounts/permissions", HTTP_POST, handleAccountPermissions},
  {"/api/accounts/archive", HTTP_POST, handleAccountArchive},
  {"/api/accounts/merge", HTTP_POST, handleAccountMerge},
  {"/api/diagnostics/log", HTTP_GET, handleSerialLogDownload},
  {"/api/devices", HTTP_GET, handleDevices},
  {"/api/device/test", HTTP_POST, handleHardwareTest},
  {"/api/device/name", HTTP_POST, handleDeviceName},
  {"/api/device/forget", HTTP_POST, handleForgetDevice},
  {"/api/pairing", HTTP_GET, handlePairingSettings},
  {"/api/pairing", HTTP_POST, handleSavePairingSettings},
  {"/api/speaker", HTTP_GET, handleSpeakerSettings},
  {"/api/speaker", HTTP_POST, handleSaveSpeakerSettings},
  {"/api/power", HTTP_GET, handlePowerSettings},
  {"/api/power", HTTP_POST, handleSavePowerSettings},
  {"/api/upload-approval", HTTP_GET, handleUploadApprovalSettings},
  {"/api/upload-approval", HTTP_POST, handleSaveUploadApprovalSettings},
  {"/api/table-code", HTTP_GET, handleTableCodeSettings},
  {"/api/table-code", HTTP_POST, handleSaveTableCodeSettings},
  {"/api/table/reset", HTTP_POST, handleResetTable},
  {"/api/device/factory-reset", HTTP_POST, handleFactoryReset},
  {"/api/device/pair-confirm", HTTP_POST, handlePairConfirm},
  {"/api/network", HTTP_GET, handleNetworkInfo},
  {"/api/network/password", HTTP_POST, handleNetworkPassword},
  {"/api/network/home", HTTP_POST, handleHomeNetwork},
  {"/api/network/home/check", HTTP_POST, handleHomeNetworkCheck},
  {"/api/network/channel", HTTP_POST, handleTableChannel},
  // First-run setup, shared by the Android app and the portal.
  {"/api/setup", HTTP_GET, handleSetupStatus},
  {"/api/setup/finish", HTTP_POST, handleSetupFinish},
  // Newer firmware, reported by the app from the public release feed.
  {"/api/updates", HTTP_GET, handleUpdateStatus},
  {"/api/updates/latest", HTTP_POST, handleLatestFirmware},

  // Profiles and sessions.
  {"/api/profiles", HTTP_GET, handleProfiles},
  {"/api/profiles/register", HTTP_POST, handleRegistration},
  {"/api/profiles/link-key", HTTP_POST, handleLinkKey},
  {"/api/profiles/sync", HTTP_POST, handleProfileSync},
  {"/api/profiles/history", HTTP_GET, handleProfileHistory},
  {"/api/session/login", HTTP_POST, handleSessionLogin},
  {"/api/session/me", HTTP_GET, handleSessionMe},
  {"/api/session/profile", HTTP_POST, handleProfile},
  {"/api/session/policy", HTTP_POST, handleProfilePolicy},
  {"/api/session/accessibility", HTTP_GET, handleAccessibility},
  {"/api/session/accessibility", HTTP_POST, handleSaveAccessibility},
  {"/api/session/personalization", HTTP_GET, handlePersonalization},
  {"/api/session/personalization", HTTP_POST, handleSavePersonalization},
  {"/api/avatars", HTTP_GET, handleAvatars},
  {"/api/session/avatar", HTTP_POST, handleAvatarUpload},
  {"/api/avatar/review", HTTP_POST, handleAvatarReview},
  {"/api/avatar", HTTP_GET, handleAvatarImage},
  {"/api/session/stats", HTTP_GET, handleProfileStats},
  {"/api/session/stats/export", HTTP_GET, handleProfileStatsExport},
  {"/api/session/logout", HTTP_POST, handleLogout},

  // Records and cards the app keeps on Atlas.
  {"/api/planes", HTTP_GET, handlePlanesCatalogInfo},
  {"/api/planes", HTTP_POST, handlePlanesCatalog},
  {"/api/planes/card", HTTP_POST, handlePlaneCard},
  {"/api/profiles/commanders", HTTP_GET, handleSavedCommanders},
  {"/api/standalone/import", HTTP_POST, handleStandaloneImport},
};

const Route *findRoute(HTTPMethod method, const String &uri) {
  for (const Route &route : ROUTES) {
    if (route.method == method && uri == route.uri) return &route;
  }
  return nullptr;
}

}  // namespace

namespace {

class RouteTableHandler final : public RequestHandler {
 public:
  bool canHandle(HTTPMethod method, String uri) override {
    return findRoute(method, uri) != nullptr;
  }
  bool handle(WebServer &server, HTTPMethod method, String uri) override {
    const Route *route = findRoute(method, uri);
    if (route == nullptr) return false;
    // Target the recurring phone workload (the hub's pushes and polls).
    // Administrative requests and log downloads stay outside tracing so
    // diagnostics don't trace themselves.
    if (route->handler == handleState || route->handler == handleHostedPublish ||
        route->handler == handleHostedInputs || route->handler == handleSessionMe) {
      TurnHub::HttpRequestTrace trace(route->uri);
      route->handler(server);
    } else {
      route->handler(server);
    }
    return true;
  }
};
RouteTableHandler routeTable;

}  // namespace

void begin(WebServer &server) {
  if (webServer != nullptr) return;  // Routes are registered once per boot.
  webServer = &server;
  makeToken(bootId);  // Public boot epoch, not an authentication credential.
  serialLog.print("ATLAS|BOOT|bootId=");
  serialLog.print(bootId);
  serialLog.print("|firmware=");
  serialLog.print(TurnHubFirmware::VERSION);
  serialLog.print("|build=");
  serialLog.print(TurnHubFirmware::BUILD_DATE);
  serialLog.print(" ");
  serialLog.print(TurnHubFirmware::BUILD_TIME);
  serialLog.print("|loopStackSizeBytes=");
#if defined(ARDUINO_ARCH_ESP32)
  serialLog.print(getArduinoLoopTaskStackSize());
#else
  serialLog.print(AtlasConfig::LOOP_TASK_STACK_BYTES);
#endif
  serialLog.print("|httpTrace=");
  serialLog.println(TURNHUB_HTTP_TRACE);
  serialLog.println(TurnHub::runtimeDiagnosticsJson());
  profileStoreReady = TurnHubProfiles::begin();

  static const char *headerKeys[] = {TOKEN_HEADER};
  server.collectHeaders(headerKeys, 1);
  server.addHandler(&routeTable);

  serialLog.print("ATLAS|WEB_API|READY|PROFILES|");
  serialLog.println(profileStoreReady ? "YES" : "NO");
}

}  // namespace TurnHubWebApi
