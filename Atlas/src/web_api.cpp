// HTTP API entry point: owns the callbacks Atlas registers, the shared
// response helpers and the route table. Handlers live in the web_*.cpp
// modules listed in web_api_internal.h.

#include "web_api.h"

#include "config.h"
#include "profile_login_page.h"
#include "serial_log.h"
#include "stats_page.h"
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

bool requireMasterButton(WebServer &server) {
  if (AtlasConfig::masterButtonPressed()) return true;
  sendJson(server, 403,
      "{\"ok\":false,\"error\":\"Hold the physical Atlas master button while saving this system setting\"}");
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

// --- Routes ---------------------------------------------------------------------------

void begin(WebServer &server) {
  if (webServer != nullptr) return;  // Routes are registered once per boot.
  webServer = &server;
  makeToken(bootId);  // Public boot epoch, not an authentication credential.
  profileStoreReady = TurnHubProfiles::begin();

  static const char *headerKeys[] = {TOKEN_HEADER};
  server.collectHeaders(headerKeys, 1);

  const auto route = [&server](const char *uri, HTTPMethod method, void (*handler)(WebServer &)) {
    server.on(uri, method, [&server, handler]() { handler(server); });
  };
  const auto control = [&server](const char *uri, WebControl webControl) {
    server.on(uri, HTTP_POST, [&server, webControl]() { runControl(server, webControl); });
  };
  const auto counter = [&server](const char *uri, TurnHub::IntentType type) {
    server.on(uri, HTTP_POST, [&server, type]() { handleCounterControl(server, type); });
  };
  const auto page = [&server](const char *uri, const char *html) {
    server.on(uri, HTTP_GET, [&server, html]() {
      server.sendHeader("Cache-Control", "no-store");
      server.send_P(200, "text/html", html);
    });
  };

  // Client contract (protocol/http-v1.md).
  route("/api/v1/info", HTTP_GET, handleInfo);
  route("/api/v1/state", HTTP_GET, handleState);

  // Pages served outside the portal.
  page("/stats", TurnHubStatsPage::STATS_HTML);
  page("/login", TurnHubLoginPage::HTML);

  // Accounts and administration.
  server.on("/api/accounts/setup", HTTP_GET, [&server]() { handleAccountSetup(server, true); });
  server.on("/api/accounts/setup", HTTP_POST, [&server]() { handleAccountSetup(server, false); });
  route("/api/accounts", HTTP_GET, handleAccounts);
  route("/api/accounts/permissions", HTTP_POST, handleAccountPermissions);
  route("/api/accounts/archive", HTTP_POST, handleAccountArchive);
  route("/api/accounts/moderate", HTTP_POST, handleModerate);
  route("/api/diagnostics/log", HTTP_GET, handleSerialLogDownload);
  route("/api/devices", HTTP_GET, handleDevices);
  route("/api/device/name", HTTP_POST, handleDeviceName);
  route("/api/device/forget", HTTP_POST, handleForgetDevice);
  route("/api/pairing", HTTP_GET, handlePairingSettings);
  route("/api/pairing", HTTP_POST, handleSavePairingSettings);
  route("/api/network", HTTP_GET, handleNetworkInfo);
  route("/api/network/password", HTTP_POST, handleNetworkPassword);

  // Profiles and sessions.
  route("/api/seats", HTTP_GET, handleSeats);
  route("/api/profiles", HTTP_GET, handleProfiles);
  route("/api/profiles/register", HTTP_POST, handleRegistration);
  server.on("/api/session/join", HTTP_POST, [&server]() { handleParticipation(server, WebControl::Join); });
  server.on("/api/session/leave", HTTP_POST, [&server]() { handleParticipation(server, WebControl::Leave); });
  route("/api/session/request", HTTP_POST, handleSessionRequest);
  route("/api/session/poll", HTTP_GET, handleSessionPoll);
  route("/api/session/login", HTTP_POST, handleSessionLogin);
  route("/api/session/me", HTTP_GET, handleSessionMe);
  route("/api/session/profile", HTTP_POST, handleProfile);
  route("/api/session/policy", HTTP_POST, handleProfilePolicy);
  route("/api/session/accessibility", HTTP_GET, handleAccessibility);
  route("/api/session/accessibility", HTTP_POST, handleSaveAccessibility);
  route("/api/session/stats", HTTP_GET, handleProfileStats);
  route("/api/session/stats/export", HTTP_GET, handleProfileStatsExport);
  route("/api/session/logout", HTTP_POST, handleLogout);

  // Game settings and counters.
  route("/api/game/settings", HTTP_GET, handleGameSettings);
  route("/api/game/settings", HTTP_POST, handleSaveGameSettings);
  route("/api/game/counters", HTTP_GET, handleCounters);
  route("/api/control/life", HTTP_POST, handleChangeLife);
  counter("/api/control/life/request", TurnHub::IntentType::RequestLifeChange);
  counter("/api/control/life/respond", TurnHub::IntentType::RespondLifeChange);
  counter("/api/control/commander", TurnHub::IntentType::ChangeCounter);

  // Session controls.
  control("/api/control/pass", WebControl::Pass);
  control("/api/control/pause", WebControl::PauseResume);
  control("/api/control/concede", WebControl::Concede);
  control("/api/control/win", WebControl::ClaimWin);
  control("/api/control/confirm", WebControl::ConfirmWin);
  control("/api/control/deny", WebControl::DenyWin);
  control("/api/control/starter", WebControl::SelectStarter);
  control("/api/control/start", WebControl::Start);
  control("/api/control/cancel-start", WebControl::CancelStart);
  control("/api/control/rematch", WebControl::Rematch);
  control("/api/control/reset", WebControl::Reset);

  serialLog.print("ATLAS|WEB_API|READY|PROFILES|");
  serialLog.println(profileStoreReady ? "YES" : "NO");
}

}  // namespace TurnHubWebApi
