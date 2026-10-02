// HTTP API entry point: owns the callbacks Atlas registers, the shared
// response helpers and the route table. Handlers live in the web_*.cpp
// modules listed in web_api_internal.h.

#include "web_api.h"

#include "config.h"
#include "profile_login_page.h"
#include "web_pages.h"
#include "serial_log.h"
#include "web_api_internal.h"

using TurnHub::serialLog;

namespace TurnHubWebApi {
namespace internal {

bool profileStoreReady = false;
char bootId[TOKEN_LENGTH + 1] = {};

ResolveSeatCallback resolveSeat = nullptr;
ControlCallback controlHandler = nullptr;
ProfileControlCallback profileControlHandler = nullptr;
ResolveProfileCallback resolveProfile = nullptr;
GameSettingsCallback readGameConfiguration = nullptr;
ConfigureGameCallback configureGameHandler = nullptr;
ChangeLifeCallback changeLifeHandler = nullptr;
ReadCountersCallback readCountersHandler = nullptr;
CounterControlCallback counterControlHandler = nullptr;
ModerateCallback moderateHandler = nullptr;
DeviceIntentCallback deviceHandler = nullptr;
PairingWindowCallback readPairingWindow = nullptr;
PresenceHooks presenceHooks;
UpdateNoticeHooks updateNoticeHooks;
SpeakerVolumeCallback readSpeakerVolume = nullptr;
SetupStageCallback readSetupStage = nullptr;
AccessibilityChangedCallback accessibilityChanged = nullptr;
StateCallback readClientState = nullptr;
RevisionCallback readClientRevision = nullptr;

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

void configure(ResolveSeatCallback resolveSeatCallback, ControlCallback controlCallback,
    ProfileControlCallback profileControlCallback, ResolveProfileCallback resolveProfileCallback) {
  resolveSeat = resolveSeatCallback;
  controlHandler = controlCallback;
  profileControlHandler = profileControlCallback;
  resolveProfile = resolveProfileCallback;
}

void configureClientState(StateCallback state, RevisionCallback revision) {
  readClientState = state;
  readClientRevision = revision;
}

void configureGameControls(GameSettingsCallback read, ConfigureGameCallback configure,
    ChangeLifeCallback life) {
  readGameConfiguration = read;
  configureGameHandler = configure;
  changeLifeHandler = life;
}

void configureCounterControls(ReadCountersCallback read, CounterControlCallback control) {
  readCountersHandler = read;
  counterControlHandler = control;
}

void configureModeration(ModerateCallback callback) {
  moderateHandler = callback;
}

void configureDevices(DeviceIntentCallback manage, PairingWindowCallback window) {
  deviceHandler = manage;
  readPairingWindow = window;
}

void configureAccessibility(AccessibilityChangedCallback callback) {
  accessibilityChanged = callback;
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
template <WebControl C> void controlRoute(WebServer &server) { runControl(server, C); }
template <TurnHub::IntentType T> void counterRoute(WebServer &server) { handleCounterControl(server, T); }
void accountSetupStatus(WebServer &server) { handleAccountSetup(server, true); }
void accountSetupCreate(WebServer &server) { handleAccountSetup(server, false); }
void joinSession(WebServer &server) { handleParticipation(server, WebControl::Join); }
void leaveSession(WebServer &server) { handleParticipation(server, WebControl::Leave); }
void statsPage(WebServer &server) { servePortalPage(server, "stats.html", TurnHubWeb::BASIC_PORTAL_HTML); }
void loginPage(WebServer &server) { servePortalPage(server, "login.html", TurnHubLoginPage::HTML); }

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
  {"/api/v1/state", HTTP_GET, handleState},

  // Pages served outside the portal.
  {"/stats", HTTP_GET, statsPage},
  {"/login", HTTP_GET, loginPage},

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
  {"/api/accounts/moderate", HTTP_POST, handleModerate},
  {"/api/diagnostics/log", HTTP_GET, handleSerialLogDownload},
  {"/api/devices", HTTP_GET, handleDevices},
  {"/api/device/name", HTTP_POST, handleDeviceName},
  {"/api/device/forget", HTTP_POST, handleForgetDevice},
  {"/api/pairing", HTTP_GET, handlePairingSettings},
  {"/api/pairing", HTTP_POST, handleSavePairingSettings},
  {"/api/speaker", HTTP_GET, handleSpeakerSettings},
  {"/api/speaker", HTTP_POST, handleSaveSpeakerSettings},
  {"/api/table/reset", HTTP_POST, handleResetTable},
  {"/api/device/factory-reset", HTTP_POST, handleFactoryReset},
  {"/api/device/pair-confirm", HTTP_POST, handlePairConfirm},
  {"/api/network", HTTP_GET, handleNetworkInfo},
  {"/api/network/password", HTTP_POST, handleNetworkPassword},
  // First-run setup, shared by the Android app and the portal.
  {"/api/setup", HTTP_GET, handleSetupStatus},
  {"/api/setup/finish", HTTP_POST, handleSetupFinish},
  // Newer firmware, reported by the app from the public release feed.
  {"/api/updates", HTTP_GET, handleUpdateStatus},
  {"/api/updates/latest", HTTP_POST, handleLatestFirmware},

  // Profiles and sessions.
  {"/api/seats", HTTP_GET, handleSeats},
  {"/api/profiles", HTTP_GET, handleProfiles},
  {"/api/profiles/register", HTTP_POST, handleRegistration},
  {"/api/session/join", HTTP_POST, joinSession},
  {"/api/session/leave", HTTP_POST, leaveSession},
  {"/api/session/request", HTTP_POST, handleSessionRequest},
  {"/api/session/poll", HTTP_GET, handleSessionPoll},
  {"/api/session/login", HTTP_POST, handleSessionLogin},
  {"/api/session/me", HTTP_GET, handleSessionMe},
  {"/api/session/profile", HTTP_POST, handleProfile},
  {"/api/session/policy", HTTP_POST, handleProfilePolicy},
  {"/api/session/accessibility", HTTP_GET, handleAccessibility},
  {"/api/session/accessibility", HTTP_POST, handleSaveAccessibility},
  {"/api/session/personalization", HTTP_GET, handlePersonalization},
  {"/api/session/personalization", HTTP_POST, handleSavePersonalization},
  {"/api/avatars", HTTP_GET, handleAvatars},
  {"/api/session/stats", HTTP_GET, handleProfileStats},
  {"/api/session/stats/export", HTTP_GET, handleProfileStatsExport},
  {"/api/session/logout", HTTP_POST, handleLogout},

  // Game settings and counters.
  {"/api/game/settings", HTTP_GET, handleGameSettings},
  {"/api/game/settings", HTTP_POST, handleSaveGameSettings},
  {"/api/game/counters", HTTP_GET, handleCounters},
  {"/api/control/life", HTTP_POST, handleChangeLife},
  {"/api/control/life/request", HTTP_POST, counterRoute<TurnHub::IntentType::RequestLifeChange>},
  {"/api/control/life/respond", HTTP_POST, counterRoute<TurnHub::IntentType::RespondLifeChange>},
  {"/api/control/commander", HTTP_POST, counterRoute<TurnHub::IntentType::ChangeCounter>},

  // Session controls.
  {"/api/control/pass", HTTP_POST, controlRoute<WebControl::Pass>},
  {"/api/control/pause", HTTP_POST, controlRoute<WebControl::PauseResume>},
  {"/api/control/concede", HTTP_POST, controlRoute<WebControl::Concede>},
  {"/api/control/win", HTTP_POST, controlRoute<WebControl::ClaimWin>},
  {"/api/control/confirm", HTTP_POST, controlRoute<WebControl::ConfirmWin>},
  {"/api/control/deny", HTTP_POST, controlRoute<WebControl::DenyWin>},
  {"/api/control/starter", HTTP_POST, controlRoute<WebControl::SelectStarter>},
  {"/api/control/start", HTTP_POST, controlRoute<WebControl::Start>},
  {"/api/control/cancel-start", HTTP_POST, controlRoute<WebControl::CancelStart>},
  {"/api/control/rematch", HTTP_POST, controlRoute<WebControl::Rematch>},
  {"/api/control/reset", HTTP_POST, controlRoute<WebControl::Reset>},
};

const Route *findRoute(HTTPMethod method, const String &uri) {
  for (const Route &route : ROUTES) {
    if (route.method == method && uri == route.uri) return &route;
  }
  return nullptr;
}

class RouteTableHandler final : public RequestHandler {
 public:
  bool canHandle(HTTPMethod method, String uri) override {
    return findRoute(method, uri) != nullptr;
  }
  bool handle(WebServer &server, HTTPMethod method, String uri) override {
    const Route *route = findRoute(method, uri);
    if (route == nullptr) return false;
    route->handler(server);
    return true;
  }
};
RouteTableHandler routeTable;
}  // namespace

void begin(WebServer &server) {
  if (webServer != nullptr) return;  // Routes are registered once per boot.
  webServer = &server;
  makeToken(bootId);  // Public boot epoch, not an authentication credential.
  profileStoreReady = TurnHubProfiles::begin();

  static const char *headerKeys[] = {TOKEN_HEADER};
  server.collectHeaders(headerKeys, 1);
  server.addHandler(&routeTable);

  serialLog.print("ATLAS|WEB_API|READY|PROFILES|");
  serialLog.println(profileStoreReady ? "YES" : "NO");
}

}  // namespace TurnHubWebApi
