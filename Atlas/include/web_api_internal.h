#pragma once

// Private state and helpers shared by the web API translation units. Only
// web_*.cpp files include this; everything else uses web_api.h.
//
//   web_api.cpp          callback registration and the route table (begin)
//   web_session.cpp      browser sessions, PINs, Sigil-press claims, login,
//                        account policy queries
//   web_profile_api.cpp  profile list/registration, profile edits, policy,
//                        statistics, participation
//   web_game_api.cpp     v1 info/state, seats, game settings, life and
//                        Commander counters, session controls
//   web_admin_api.cpp    devices, network, serial log, account administration
//   web_tablet_api.cpp   tablet mode: one shared table screen for every player
//   web_standalone_api.cpp  finished standalone tablet games, imported once

#include <Arduino.h>
#include <WebServer.h>

#include "json_text.h"
#include "profile_store.h"
#include "sigil_bus.h"
#include "turnhub_types.h"
#include "web_api.h"

namespace TurnHubWebApi {
namespace internal {

using TurnHub::INVALID_ID;
using TurnHub::jsonBool;
using TurnHub::jsonEscape;
using TurnHub::MAX_PHYSICAL_SIGILS;
using TurnHub::MAX_PLAYERS;
using TurnHub::SigilBus;
using TurnHub::SigilRecord;

constexpr uint32_t CLAIM_TIMEOUT_MS = 30000;
constexpr uint32_t SESSION_TIMEOUT_MS = 8UL * 60UL * 60UL * 1000UL;
constexpr uint8_t MAX_PENDING_CLAIMS = 6;
constexpr uint8_t MAX_WEB_SESSIONS = MAX_PLAYERS * 2;
constexpr size_t TOKEN_LENGTH = 32;
constexpr size_t PIN_HASH_LENGTH = 64;
constexpr size_t MAX_NAME_LENGTH = 32;
// A profile password (the alternative to a 4-8 digit PIN), in bytes.
constexpr size_t MIN_PASSWORD_LENGTH = 8;
constexpr size_t MAX_PASSWORD_LENGTH = 64;
constexpr char TOKEN_HEADER[] = "X-TurnHub-Token";

// A browser waiting for someone to choose Link phone on a physical Sigil,
// which proves possession and authorizes (or attaches) that seat.
struct PendingClaim {
  bool used = false;
  uint64_t requestId = 0;
  uint8_t controllerId = INVALID_ID;
  uint8_t slot = 1;
  uint32_t createdMs = 0;
  bool approved = false;
  char token[TOKEN_LENGTH + 1] = {};
  char requestingToken[TOKEN_LENGTH + 1] = {};
  char profileId[TurnHubProfiles::PROFILE_ID_LENGTH + 1] = {};
  char error[100] = {};
};

// An authenticated browser. controllerId/slot are re-resolved from the
// profile on each use because table seats can move.
struct WebSession {
  bool used = false;
  uint8_t controllerId = INVALID_ID;
  uint8_t slot = 1;
  char token[TOKEN_LENGTH + 1] = {};
  char profileId[TurnHubProfiles::PROFILE_ID_LENGTH + 1] = {};
  uint32_t lastSeenMs = 0;
  // True when this browser proved knowledge of the profile's PIN (PIN login,
  // registration or setting a PIN). Sigil-press claims are not PIN-verified.
  // Gates the private moderation history.
  bool pinVerified = false;
  // Tablet mode: a table presence code turned this browser into the shared
  // table screen, which seats players and acts for any of them.
  bool tableDevice = false;
  // Venue tables: the game (0-based) this browser follows while its profile
  // isn't seated in one, and the one a table tablet serves.
  uint8_t table = 0;
};

// --- Shared state (defined in web_api.cpp / web_session.cpp) -----------------

extern PendingClaim pendingClaims[MAX_PENDING_CLAIMS];
extern WebSession sessions[MAX_WEB_SESSIONS];
extern bool profileStoreReady;
// Public per-boot epoch reported with state revisions; not a credential.
extern char bootId[TOKEN_LENGTH + 1];

extern TableHooks tableHooks;
extern ResolveSeatCallback resolveSeat;
extern ControlCallback controlHandler;
extern ProfileControlCallback profileControlHandler;
extern ResolveProfileCallback resolveProfile;
extern GameSettingsCallback readGameConfiguration;
extern ConfigureGameCallback configureGameHandler;
extern ChangeLifeCallback changeLifeHandler;
extern ReadCountersCallback readCountersHandler;
extern CounterControlCallback counterControlHandler;
extern ModerateCallback moderateHandler;
extern DeviceIntentCallback deviceHandler;
extern PairingWindowCallback readPairingWindow;
extern PresenceHooks presenceHooks;
extern UpdateNoticeHooks updateNoticeHooks;
extern SpeakerVolumeCallback readSpeakerVolume;
extern SetupStageCallback readSetupStage;
extern AccessibilityChangedCallback accessibilityChanged;
extern StateCallback readClientState;
extern RevisionCallback readClientRevision;

// --- Responses (web_api.cpp) ---------------------------------------------------

void sendJson(WebServer &server, int status, const String &body);
// {"error":"<message>"}
void sendError(WebServer &server, int status, const String &message);
// {"ok":true,"message":"<message>"}
void sendOkMessage(WebServer &server, const String &message);
// True while the signed-in profile of this request is verified at the table.
bool physicalPresence(WebServer &server);
// Sends 403 {"presenceRequired":true} unless it is.
bool requirePhysicalPresence(WebServer &server);

// --- Identity helpers (web_admin_api.cpp) -------------------------------------

String atlasHardwareId();
String sigilHardwareId(const uint8_t mac[6]);
const SigilRecord *recordForModule(uint8_t controllerId);

// --- Sessions and PINs (web_session.cpp) ---------------------------------------

void makeToken(char out[TOKEN_LENGTH + 1]);
// Expires stale claims and sessions.
void cleanup(uint32_t nowMs);
WebSession *sessionForToken(const String &token, uint32_t nowMs);
WebSession *sessionForRequest(WebServer &server);
WebSession *createProfileSession(const String &profileId, uint32_t nowMs, bool pinVerified);
// {"ok":true,"token":...,"profileId":...}, or 503 when no session slot is free.
void sendLogin(WebServer &server, WebSession *session);
bool resolveSessionParticipant(WebSession &session);
// Checks another profile's PIN or password, rate limited like a sign-in, and
// admits the account (archived ones are refused). Sends the error.
bool verifyProfileSecret(WebServer &server, const String &profileId, const String &pin);
// The session's profile ID if the profile still exists, else empty.
String sessionProfileId(const WebSession &session);
// Expires stale sessions and re-resolves every session's seat once, so a
// listing can then ask seatHasSession / moduleSessionCount per seat cheaply.
void resolveAllSessions(uint32_t nowMs);
bool seatHasSession(uint8_t controllerId, uint8_t slot);
uint8_t moduleSessionCount(uint8_t controllerId);
bool validSlot(int slot);
bool resolveSeatNow(uint8_t controllerId, uint8_t slot, SeatSnapshot &snapshot);
String profileIdForPhysicalSeat(uint8_t controllerId, uint8_t slot);
bool hasPin(uint8_t controllerId, uint8_t slot);
bool validPin(const String &pin);
String profilePinHash(const String &profileId, const String &pin);
// Trims and truncates a user-supplied display name.
String cleanName(const String &raw);

void handleSessionRequest(WebServer &server);
void handleSessionPoll(WebServer &server);
void handleSessionLogin(WebServer &server);
void handleSessionMe(WebServer &server);
void handleLogout(WebServer &server);
void handleSessionGame(WebServer &server);

// --- Profiles (web_profile_api.cpp) ----------------------------------------------

void handleProfiles(WebServer &server);
void handleRegistration(WebServer &server);
void handleParticipation(WebServer &server, WebControl control);
void handleProfile(WebServer &server);
void handleProfilePolicy(WebServer &server);
void handleAccessibility(WebServer &server);
void handlePersonalization(WebServer &server);
void handleSavePersonalization(WebServer &server);
void handleAvatars(WebServer &server);
void handleSaveAccessibility(WebServer &server);
void handleProfileStats(WebServer &server);
void handleProfileStatsExport(WebServer &server);

// --- Standalone tablet games (web_standalone_api.cpp) -----------------------------

void handleStandaloneImport(WebServer &server);

// --- Game (web_game_api.cpp) -------------------------------------------------------

void handleInfo(WebServer &server);
void handleState(WebServer &server);
void handleSeats(WebServer &server);
void handleGameSettings(WebServer &server);
void handleSaveGameSettings(WebServer &server);
// Next-game settings from the request over the current ones; sends 400 when invalid.
bool parseGameSettings(WebServer &server, TurnHub::GameSettings &settings);
void handleChangeLife(WebServer &server);
void handleCounters(WebServer &server);
void handleCounterControl(WebServer &server, TurnHub::IntentType type);
// Life or Commander request fields of a counter control; false when invalid.
bool parseCounterPayload(WebServer &server, TurnHub::IntentType type, TurnHub::IntentPayload &payload);
bool parseBoundedNumber(const String &text, int32_t min, int32_t max, int32_t &value);
bool parseLifeInteger(const String &text, int32_t &value, bool negativeAllowed);
// {"ok":true} or 409 with the seat callback's message.
void sendSeatResult(WebServer &server, bool accepted, const String &message);
void runControl(WebServer &server, WebControl control);
// A session control for a seat already authorized: optional expectedRevision,
// then the control callback; answers like /api/control/*.
void runSeatControl(WebServer &server, uint8_t controllerId, uint8_t slot, WebControl control);

// --- Administration (web_admin_api.cpp) ---------------------------------------------

void handleDevices(WebServer &server);
void handleDeviceName(WebServer &server);
void handleForgetDevice(WebServer &server);
void handlePairingSettings(WebServer &server);
void handleSavePairingSettings(WebServer &server);
void handleSpeakerSettings(WebServer &server);
void handleSaveSpeakerSettings(WebServer &server);
void handleTableCodeSettings(WebServer &server);
void handleSaveTableCodeSettings(WebServer &server);
void handleResetTable(WebServer &server);
void handleFactoryReset(WebServer &server);
void handlePairConfirm(WebServer &server);
void handleNetworkInfo(WebServer &server);
void handleNetworkPassword(WebServer &server);
void handleSetupStatus(WebServer &server);
void handleSetupFinish(WebServer &server);
void handleSerialLogDownload(WebServer &server);
// Newer firmware: GET /api/updates, POST /api/updates/latest.
void handleUpdateStatus(WebServer &server);
void handleLatestFirmware(WebServer &server);
void handleAccountSetup(WebServer &server, bool readOnly);
// Table presence: GET /api/presence, POST /api/presence/request, /confirm, /lock.
void handlePresenceStatus(WebServer &server);
void handlePresenceRequest(WebServer &server);
void handlePresenceConfirm(WebServer &server);
void handlePresenceLock(WebServer &server);
void handleAccounts(WebServer &server);
void handleAccountPermissions(WebServer &server);
void handleAccountArchive(WebServer &server);
void handleModerate(WebServer &server);

// --- Tablet mode (web_tablet_api.cpp) ------------------------------------------------

void handleTabletEnable(WebServer &server);
void handleTabletDisable(WebServer &server);
void handleTabletSeat(WebServer &server);
void handleTabletUnseat(WebServer &server);
void handleTabletControl(WebServer &server);
void handleTabletLife(WebServer &server);
void handleTabletSettings(WebServer &server);
void handleTabletCounter(WebServer &server, TurnHub::IntentType type);

}  // namespace internal
}  // namespace TurnHubWebApi
