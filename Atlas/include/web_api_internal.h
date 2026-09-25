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
constexpr char TOKEN_HEADER[] = "X-TurnHub-Token";

// A browser waiting for someone to press Action on a physical Sigil, which
// proves possession and authorizes (or attaches) that seat.
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
};

// --- Shared state (defined in web_api.cpp / web_session.cpp) -----------------

extern PendingClaim pendingClaims[MAX_PENDING_CLAIMS];
extern WebSession sessions[MAX_WEB_SESSIONS];
extern bool profileStoreReady;
// Public per-boot epoch reported with state revisions; not a credential.
extern char bootId[TOKEN_LENGTH + 1];

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
extern SpeakerVolumeCallback readSpeakerVolume;
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
// The session's profile ID if the profile still exists, else empty.
String sessionProfileId(const WebSession &session);
bool seatHasSession(uint8_t controllerId, uint8_t slot, uint32_t nowMs);
uint8_t moduleSessionCount(uint8_t controllerId, uint32_t nowMs);
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

// --- Profiles (web_profile_api.cpp) ----------------------------------------------

void handleProfiles(WebServer &server);
void handleRegistration(WebServer &server);
void handleParticipation(WebServer &server, WebControl control);
void handleProfile(WebServer &server);
void handleProfilePolicy(WebServer &server);
void handleAccessibility(WebServer &server);
void handleSaveAccessibility(WebServer &server);
void handleProfileStats(WebServer &server);
void handleProfileStatsExport(WebServer &server);

// --- Game (web_game_api.cpp) -------------------------------------------------------

void handleInfo(WebServer &server);
void handleState(WebServer &server);
void handleSeats(WebServer &server);
void handleGameSettings(WebServer &server);
void handleSaveGameSettings(WebServer &server);
void handleChangeLife(WebServer &server);
void handleCounters(WebServer &server);
void handleCounterControl(WebServer &server, TurnHub::IntentType type);
void runControl(WebServer &server, WebControl control);

// --- Administration (web_admin_api.cpp) ---------------------------------------------

void handleDevices(WebServer &server);
void handleDeviceName(WebServer &server);
void handleForgetDevice(WebServer &server);
void handlePairingSettings(WebServer &server);
void handleSavePairingSettings(WebServer &server);
void handleSpeakerSettings(WebServer &server);
void handleSaveSpeakerSettings(WebServer &server);
void handleResetTable(WebServer &server);
void handleFactoryReset(WebServer &server);
void handleNetworkInfo(WebServer &server);
void handleNetworkPassword(WebServer &server);
void handleSerialLogDownload(WebServer &server);
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

}  // namespace internal
}  // namespace TurnHubWebApi
