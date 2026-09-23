#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "game_profile.h"
#include "account_access.h"
#include "game_engine.h"
#include "intent.h"

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
  AttachPhysical,
};

struct SeatSnapshot {
  bool exists = false;
  uint8_t playerNumber = 0;
  bool active = false;
  bool eliminated = false;
  bool host = false;
  bool lifeAvailable = false;
  int32_t life = 0;
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
void serveRestrictedPage(WebServer &server,const char *html,uint8_t permission);
bool connectionBlocked(const String &id);
void revokeConnections(const String &id);
using ModerateCallback = bool (*)(const String &actor,const String &target,const String &action,String &message);
void configureModeration(ModerateCallback callback);


// Called only for real physical Sigil button activity. A pending browser claim
// is approved when the user proves possession by pressing Action on that Sigil.
void notePhysicalAction(uint8_t sigilId);

// Atlas policy queries. Session checks do not refresh authentication lifetime.
bool profileAuthenticated(const String &profileId);
bool physicalUseAllowed(const String &profileId);
bool physicalStatsVisible(const String &profileId);

}  // namespace TurnHubWebApi
