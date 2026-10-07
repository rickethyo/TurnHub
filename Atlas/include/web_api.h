#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "game_profile.h"
#include "account_access.h"
#include "game_engine.h"
#include "intent.h"
#include "update_notice.h"

namespace TurnHubWebApi {

using StateCallback = String (*)(const String &atlasId, const char *bootId);
using RevisionCallback = uint32_t (*)();
void configureClientState(StateCallback state, RevisionCallback revision);

enum class WebControl : uint8_t {
  Pass,
  PauseResume,
  Concede,
  ClaimWin,
  ConfirmWin,
  DenyWin,
  SelectStarter,
  Start,
  CancelStart,
  Rematch,
  Reset,
  Join,
  Leave,
  Nudge,
  AttachPhysical,
  // Turn order in the lobby; offered only by /api/tablet/control.
  MoveEarlier,
  MoveLater,
};

struct SeatSnapshot {
  bool exists = false;
  uint8_t playerNumber = 0;
  bool active = false;
  bool eliminated = false;
  bool host = false;
  bool lifeAvailable = false;
  int32_t life = 0;
  uint8_t team = 0;     // Two-Headed Giant team (lobby: the next game's), else 0.
  bool winner = false;  // The winner, or the winner's teammate.
};

using ResolveSeatCallback = bool (*)(
    uint8_t controllerId,
    uint8_t slot,
    SeatSnapshot &snapshot);

using ControlCallback = bool (*)(
    uint8_t controllerId,
    uint8_t slot,
    WebControl control,
    String &message);

using ProfileControlCallback = bool (*)(const String &profileId, WebControl control,
    uint8_t controllerId, uint8_t slot, String &message);
using ResolveProfileCallback = bool (*)(const String &profileId,
    uint8_t &controllerId, uint8_t &slot);

using GameSettingsCallback = bool (*)(TurnHub::GameSettings &settings, bool &editable);
using ConfigureGameCallback = bool (*)(uint8_t controllerId, uint8_t slot,
    const TurnHub::GameSettings &settings, String &message);
using ChangeLifeCallback = bool (*)(uint8_t controllerId, uint8_t slot, int32_t delta, String &message);
void configureGameControls(GameSettingsCallback read, ConfigureGameCallback configure, ChangeLifeCallback life);

struct CounterSnapshot {
  bool editable = false;
  bool commanderEnabled = false;
  uint8_t player = 0;
  uint8_t playerCount = 0;
  uint8_t sources[TurnHub::MAX_PLAYERS] = {};
  int32_t damage[TurnHub::MAX_PLAYERS][TurnHub::COMMANDERS_PER_PLAYER] = {};
  TurnHub::LifeChangeRequest requests[TurnHub::MAX_PLAYERS] = {};
};
using ReadCountersCallback = bool (*)(uint8_t controller, uint8_t slot, CounterSnapshot &snapshot);
using CounterControlCallback = bool (*)(uint8_t controller, uint8_t slot, TurnHub::IntentType type,
    const TurnHub::IntentPayload &payload, String &message);
void configureCounterControls(ReadCountersCallback read, CounterControlCallback control);

// Connect the web layer to the authoritative Atlas lobby/game state.
void configure(
    ResolveSeatCallback resolveSeatCallback,
    ControlCallback controlCallback,
    ProfileControlCallback profileControlCallback,
    ResolveProfileCallback resolveProfileCallback);

// Register device-management, browser-session, profile, and authenticated
// web-control endpoints on the Atlas WebServer.
void begin(WebServer &server);
bool requirePermission(WebServer &server,uint8_t permission);
// The same check without sending an error response: upload callbacks use it,
// because a response sent mid-upload leaves the browser waiting.
bool hasPermission(WebServer &server,uint8_t permission);
// The request's signed-in profile is verified at the table (presence code).
bool verifiedAtTable(WebServer &server);
// Pages the SD portal pack may replace (WEB_PORTAL.md, "Serving"): the
// pack's `packFile` when one is installed, else the built-in `html`. A
// `?classic=1` request always gets the built-in page.
void servePortalPage(WebServer &server,const char *packFile,const char *html);
// The same for pages that need an account permission. Without a token the
// browser gets a small loader that asks again with its saved session token.
void serveRestrictedPage(WebServer &server,const char *html,uint8_t permission,const char *packFile=nullptr);
bool connectionBlocked(const String &id);
void revokeConnections(const String &id);
using ModerateCallback = bool (*)(const String &actor,const String &target,const String &action,String &message);
void configureModeration(ModerateCallback callback);
// Admin device and table management (ForgetPairing, ConfigurePairing,
// ConfigureSpeaker, PairConfirm, ResetTable, FactoryReset, UpdateSigil,
// AdvanceSetup; intent.h documents each value). actor is the signed-in
// account; Atlas re-checks its Admin permission.
using DeviceIntentCallback = bool (*)(const String &actor, TurnHub::IntentType type,
    int32_t value, String &message);
using PairingWindowCallback = uint32_t (*)();
void configureDevices(DeviceIntentCallback manage, PairingWindowCallback window);
// Atlas speaker volume (0 off to 3 high). Saving goes through the device
// callback as ConfigureSpeaker.
using SpeakerVolumeCallback = uint8_t (*)();
void configureSpeaker(SpeakerVolumeCallback volume);

// First-run setup: the current TurnHub::SetupStage, for GET /api/setup. The
// stage itself changes through the device callback (IntentType::AdvanceSetup).
using SetupStageCallback = uint8_t (*)();
void configureSetup(SetupStageCallback stage);
// Told after a profile's accessibility preferences were saved, so Atlas can
// restyle that player's Sigil straight away.
using AccessibilityChangedCallback = void (*)();
void configureAccessibility(AccessibilityChangedCallback callback);
// Table presence (front_panel.cpp): a code the Atlas screen shows, entered on
// the phone, verifies that profile is at the table for a while. First-Admin
// setup, system settings, device names, OTA, Return to lobby and factory
// reset require it on top of the account permission.
enum class PresenceResult : uint8_t { Verified, WrongCode, NoCode, TooManyAttempts };
struct PresenceHooks {
  uint32_t (*remainingMs)(const String &profileId) = nullptr;  // 0: not verified.
  bool (*request)(const String &profileId, bool setup) = nullptr;
  PresenceResult (*confirm)(const String &profileId, uint32_t code) = nullptr;
  void (*revoke)(const String &profileId) = nullptr;
};
void configurePresence(const PresenceHooks &hooks);
// Newer firmware (update_notice.h, front_panel.cpp). POST /api/updates/latest
// (public: the release feed is public, and a report can only light the LED)
// takes `atlas`, `sigilEink` and `sigilOled` versions; GET /api/updates
// reports them and how many devices are behind.
struct UpdateNoticeHooks {
  void (*note)(const TurnHub::LatestFirmware &latest) = nullptr;
  const TurnHub::LatestFirmware *(*latest)() = nullptr;
  uint8_t (*available)() = nullptr;
};
void configureUpdateNotice(const UpdateNoticeHooks &hooks);

// Venue tables (PLANNED_DESIGNS.md): Atlas runs `count` games side by side.
// Each request acts on one: a table tablet's chosen game; else the game the
// session's profile plays in; else the game the session follows (POST
// /api/session/game, game=1..count); without a session, `game` (1-based) if
// given, else Game 1.
struct TableHooks {
  uint8_t count = 1;
  int8_t (*profileTable)(const String &profileId) = nullptr;  // -1: not seated.
  uint8_t (*select)(uint8_t index) = nullptr;                 // Returns the previous one.
};
void configureTables(const TableHooks &hooks);
// The game (0-based) a request acts on, for routes outside the route table.
uint8_t requestGame(WebServer &server);

// Called only for real physical Sigil activity. A pending browser claim is
// approved when the user proves possession by choosing Link phone on that Sigil.
void notePhysicalAction(uint8_t sigilId);
// True while a browser waits for someone to confirm on this Sigil; menu
// Sigils then offer Link phone (SigilAction::LinkPhone).
bool hasPendingClaim(uint8_t sigilId);

// Atlas policy queries. Session checks do not refresh authentication lifetime.
bool profileAuthenticated(const String &profileId);
bool physicalUseAllowed(const String &profileId);
bool physicalStatsVisible(const String &profileId);

}  // namespace TurnHubWebApi
