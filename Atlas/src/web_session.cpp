// Browser authentication: session tokens, PIN hashing and verification,
// the Link-phone-on-the-Sigil claim flow, profile/seat login, and the
// account policy queries Atlas uses to gate physical play.

#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>

#include "account_access.h"
#include "controller_profiles.h"
#include "login_limiter.h"
#include "serial_log.h"
#include "web_api_internal.h"

using TurnHub::serialLog;

namespace TurnHubWebApi {
namespace internal {

PendingClaim pendingClaims[MAX_PENDING_CLAIMS];
WebSession sessions[MAX_WEB_SESSIONS];

namespace {

constexpr size_t REQUEST_ID_LENGTH = 16;
TurnHub::LoginLimiter loginLimiter;

uint64_t random64() {
  return (static_cast<uint64_t>(esp_random()) << 32) | static_cast<uint64_t>(esp_random());
}

String requestIdText(uint64_t value) {
  char text[REQUEST_ID_LENGTH + 1];
  snprintf(text, sizeof(text), "%08lX%08lX",
      static_cast<unsigned long>(value >> 32),
      static_cast<unsigned long>(value & 0xFFFFFFFFULL));
  return String(text);
}

uint64_t parseRequestId(const String &value) {
  if (value.length() != REQUEST_ID_LENGTH) return 0;
  char *end = nullptr;
  const uint64_t parsed = strtoull(value.c_str(), &end, 16);
  return end != nullptr && *end == '\0' ? parsed : 0;
}

String sha256Hex(const String &material) {
  uint8_t digest[32] = {};
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts_ret(&context, 0) != 0 ||
      mbedtls_sha256_update_ret(&context,
          reinterpret_cast<const unsigned char *>(material.c_str()), material.length()) != 0 ||
      mbedtls_sha256_finish_ret(&context, digest) != 0) {
    mbedtls_sha256_free(&context);
    return String();
  }
  mbedtls_sha256_free(&context);

  char hex[PIN_HASH_LENGTH + 1];
  for (uint8_t i = 0; i < 32; ++i) snprintf(hex + (i * 2), 3, "%02x", digest[i]);
  hex[PIN_HASH_LENGTH] = '\0';
  return String(hex);
}

bool pinMatches(uint8_t controllerId, uint8_t slot, const String &pin) {
  const SigilRecord *record = recordForModule(controllerId);
  if (record == nullptr) return false;

  const String profileId = TurnHubProfiles::profileIdForSeat(record->mac, slot);
  if (!TurnHubProfiles::profileExists(profileId)) return false;

  const String stored = TurnHubProfiles::storedPinHashForSeat(record->mac, slot);
  const String candidate = profilePinHash(profileId, pin);
  return stored.length() == PIN_HASH_LENGTH && candidate.length() == PIN_HASH_LENGTH &&
      stored.equalsIgnoreCase(candidate);
}

WebSession *createSession(uint8_t controllerId, uint8_t slot, uint32_t nowMs, bool pinVerified) {
  WebSession *session =
      createProfileSession(profileIdForPhysicalSeat(controllerId, slot), nowMs, pinVerified);
  if (session) {
    session->controllerId = controllerId;
    session->slot = slot;
  }
  return session;
}

// Admits a signed-in account, clearing a moderation-forced reconnect.
bool admitAccount(WebServer &server, const String &profileId) {
  TurnHubAccounts::Account account;
  if (!TurnHubAccounts::load(profileId, account) || account.archived) {
    sendError(server, 403, "Account unavailable or archived");
    return false;
  }
  if (account.reconnectRequired) {
    account.reconnectRequired = false;
    if (!TurnHubAccounts::save(profileId, account)) {
      sendError(server, 503, "Could not reconnect");
      return false;
    }
  }
  return true;
}

bool admitRateLimited(WebServer &server, const String &profileId) {
  if (loginLimiter.allow(profileId.c_str(), millis())) return true;
  sendError(server, 429, "Too many attempts. Wait 30 seconds and retry");
  return false;
}

void handleProfileLogin(WebServer &server) {
  const String id = server.arg("profileId");
  const String pin = server.arg("pin");
  if (!validPin(pin) || !TurnHubProfiles::profileExists(id)) {
    sendError(server, 401, "Profile or PIN was not accepted");
    return;
  }
  if (!admitRateLimited(server, id)) return;
  const String stored = TurnHubProfiles::storedPinHashForProfile(id);
  // A profile with no PIN yet (made on a Sigil or the tablet) is claimed by
  // its first phone sign-in: the PIN given here becomes its PIN (owner
  // request 2026-10-07). Clients ask for it twice before sending.
  if (!TurnHubProfiles::hasPinForProfile(id)) {
    if (!admitAccount(server, id)) return;
    const String hash = profilePinHash(id, pin);
    if (hash.length() != PIN_HASH_LENGTH || !TurnHubProfiles::setPinHashForProfile(id, hash)) {
      sendError(server, 503, "Could not save PIN");
      return;
    }
    serialLog.print("ATLAS|PROFILE|PIN_SET_AT_SIGN_IN|");
    serialLog.println(id);
    sendLogin(server, createProfileSession(id, millis(), true));
    return;
  }
  if (stored.length() != PIN_HASH_LENGTH || !stored.equalsIgnoreCase(profilePinHash(id, pin))) {
    sendError(server, 401, "Profile or PIN was not accepted");
    return;
  }
  loginLimiter.success(id.c_str());
  if (!admitAccount(server, id)) return;
  sendLogin(server, createProfileSession(id, millis(), true));
}

// Seat login (the portal's Sign in on a Sigil seat): the PIN is checked for the
// profile bound to that seat.
void handleSeatLogin(WebServer &server) {
  if (!server.hasArg("module") || !server.hasArg("slot") || !server.hasArg("pin")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Module, slot, and PIN are required\"}");
    return;
  }
  const int module = server.arg("module").toInt();
  const int slot = server.arg("slot").toInt();
  SeatSnapshot snapshot;
  if (module < 0 || module >= MAX_PHYSICAL_SIGILS || !validSlot(slot) ||
      !resolveSeatNow(static_cast<uint8_t>(module), static_cast<uint8_t>(slot), snapshot)) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Seat is not available\"}");
    return;
  }
  const uint8_t controllerId = static_cast<uint8_t>(module);
  const uint8_t seatSlot = static_cast<uint8_t>(slot);

  const String loginProfile = profileIdForPhysicalSeat(controllerId, seatSlot);
  if (!admitRateLimited(server, loginProfile)) return;
  if (!validPin(server.arg("pin")) || !pinMatches(controllerId, seatSlot, server.arg("pin"))) {
    sendJson(server, 401, "{\"ok\":false,\"error\":\"Incorrect PIN\"}");
    return;
  }
  loginLimiter.success(loginProfile.c_str());
  if (!admitAccount(server, loginProfile)) return;

  WebSession *session = createSession(controllerId, seatSlot, millis(), true);
  if (session == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"No session slots available\"}");
    return;
  }
  String response = "{\"ok\":true,\"token\":\"";
  response += session->token;
  response += "\",\"module\":";
  response += String(module);
  response += ",\"slot\":";
  response += String(slot);
  response += ",\"profileId\":\"";
  response += jsonEscape(sessionProfileId(*session));
  response += "\"}";
  sendJson(server, 200, response);
}

}  // namespace

bool verifyProfileSecret(WebServer &server, const String &profileId, const String &pin) {
  if (!admitRateLimited(server, profileId)) return false;
  const String stored = TurnHubProfiles::storedPinHashForProfile(profileId);
  if (!validPin(pin) || stored.length() != PIN_HASH_LENGTH ||
      !stored.equalsIgnoreCase(profilePinHash(profileId, pin))) {
    sendError(server, 401, "That PIN was not accepted");
    return false;
  }
  loginLimiter.success(profileId.c_str());
  return admitAccount(server, profileId);
}

// --- Tokens, sessions and seats ------------------------------------------------

void makeToken(char out[TOKEN_LENGTH + 1]) {
  const uint32_t a = esp_random();
  const uint32_t b = esp_random();
  const uint32_t c = esp_random();
  const uint32_t d = esp_random();
  snprintf(out, TOKEN_LENGTH + 1, "%08lX%08lX%08lX%08lX",
      static_cast<unsigned long>(a), static_cast<unsigned long>(b),
      static_cast<unsigned long>(c), static_cast<unsigned long>(d));
}

void cleanup(uint32_t nowMs) {
  for (auto &pending : pendingClaims) {
    if (pending.used && nowMs - pending.createdMs > CLAIM_TIMEOUT_MS) pending = PendingClaim{};
  }
  for (auto &session : sessions) {
    if (session.used && nowMs - session.lastSeenMs > SESSION_TIMEOUT_MS) session = WebSession{};
  }
}

WebSession *sessionForToken(const String &token, uint32_t nowMs) {
  cleanup(nowMs);
  if (token.length() != TOKEN_LENGTH) return nullptr;
  for (auto &session : sessions) {
    if (!session.used || !token.equalsIgnoreCase(session.token)) continue;
    // Archiving an account ends its sessions on their next use.
    TurnHubAccounts::Account account;
    if (!TurnHubAccounts::load(String(session.profileId), account) || account.archived) {
      session = WebSession{};
      return nullptr;
    }
    session.lastSeenMs = nowMs;
    return &session;
  }
  return nullptr;
}

WebSession *sessionForRequest(WebServer &server) {
  return sessionForToken(server.header(TOKEN_HEADER), millis());
}

WebSession *createProfileSession(const String &profileId, uint32_t nowMs, bool pinVerified) {
  cleanup(nowMs);
  TurnHubAccounts::Account account;
  if (!TurnHubAccounts::load(profileId, account) || account.archived) return nullptr;
  if (!TurnHubProfiles::profileExists(profileId)) return nullptr;

  for (auto &session : sessions) {
    if (session.used) continue;
    session = WebSession{};
    session.used = true;
    session.lastSeenMs = nowMs;
    session.pinVerified = pinVerified;
    makeToken(session.token);
    strncpy(session.profileId, profileId.c_str(), sizeof(session.profileId) - 1);
    return &session;
  }
  return nullptr;
}

void sendLogin(WebServer &server, WebSession *session) {
  if (!session) {
    sendError(server, 503, "No session slots available");
    return;
  }
  sendJson(server, 200, String("{\"ok\":true,\"token\":\"") + session->token +
      "\",\"profileId\":\"" + session->profileId + "\"}");
}

bool resolveSessionParticipant(WebSession &session) {
  session.controllerId = INVALID_ID;
  session.slot = 1;
  return resolveProfile &&
      resolveProfile(String(session.profileId), session.controllerId, session.slot);
}

String sessionProfileId(const WebSession &session) {
  const String profileId(session.profileId);
  return TurnHubProfiles::profileExists(profileId) ? profileId : String();
}

void resolveAllSessions(uint32_t nowMs) {
  cleanup(nowMs);
  for (auto &session : sessions) {
    if (session.used) resolveSessionParticipant(session);
  }
}

bool seatHasSession(uint8_t controllerId, uint8_t slot) {
  for (const auto &session : sessions) {
    if (session.used && session.controllerId == controllerId && session.slot == slot) return true;
  }
  return false;
}

uint8_t moduleSessionCount(uint8_t controllerId) {
  uint8_t count = 0;
  for (const auto &session : sessions) {
    if (session.used && session.controllerId == controllerId) ++count;
  }
  return count;
}

bool validSlot(int slot) {
  return slot == 1 || slot == 2;
}

bool resolveSeatNow(uint8_t controllerId, uint8_t slot, SeatSnapshot &snapshot) {
  snapshot = SeatSnapshot{};
  if (resolveSeat == nullptr || !validSlot(slot)) return false;
  return resolveSeat(controllerId, slot, snapshot) && snapshot.exists;
}

String profileIdForPhysicalSeat(uint8_t controllerId, uint8_t slot) {
  return TurnHubControllers::profileForSeat(controllerId, slot);
}

// --- PINs and names ---------------------------------------------------------------

bool hasPin(uint8_t controllerId, uint8_t slot) {
  if (TurnHubProfiles::hasPinForProfile(profileIdForPhysicalSeat(controllerId, slot))) return true;
  const SigilRecord *record = recordForModule(controllerId);
  return record != nullptr && TurnHubProfiles::hasPinForSeat(record->mac, slot);
}

// A profile secret is a PIN (4 to 8 decimal digits) or a password (8 to 64
// bytes, no control characters; UTF-8 is fine). Both are hashed and checked
// the same way, so the rest of Atlas only ever sees "the PIN".
bool validPin(const String &pin) {
  const size_t length = pin.length();
  bool digitsOnly = true;
  for (size_t i = 0; i < length; ++i) {
    const uint8_t c = static_cast<uint8_t>(pin[i]);
    if (c < 0x20 || c == 0x7F) return false;
    if (c < '0' || c > '9') digitsOnly = false;
  }
  if (digitsOnly && length >= 4 && length <= 8) return true;
  return length >= MIN_PASSWORD_LENGTH && length <= MAX_PASSWORD_LENGTH;
}

String profilePinHash(const String &profileId, const String &pin) {
  TurnHubIdentity::ProfileId parsed;
  if (!TurnHubIdentity::ProfileId::parse(profileId.c_str(), parsed)) return String();
  String material = "TurnHubProfile:";
  material += profileId;
  material += ':';
  material += pin;
  return sha256Hex(material);
}

String cleanName(const String &raw) {
  String name = raw;
  name.trim();
  if (name.length() > MAX_NAME_LENGTH) name.remove(MAX_NAME_LENGTH);
  return name;
}

// --- Sigil-press claims ------------------------------------------------------------

// Starts a claim: the browser asks for a seat and someone proves possession by
// choosing Link phone on that Sigil (see notePhysicalAction). A signed-in browser
// uses the same flow to attach its profile to the Sigil.
void handleSessionRequest(WebServer &server) {
  SigilBus *bus = SigilBus::activeInstance();
  if (bus == nullptr || !server.hasArg("module") || !server.hasArg("slot")) {
    sendJson(server, 400, "{\"ok\":false,\"error\":\"Missing seat\"}");
    return;
  }

  const int module = server.arg("module").toInt();
  const int slot = server.arg("slot").toInt();
  WebSession *requester = sessionForRequest(server);
  SeatSnapshot snapshot;
  if (module < 0 || module >= MAX_PHYSICAL_SIGILS || !validSlot(slot) ||
      !bus->isOnline(static_cast<uint8_t>(module), millis()) ||
      (!requester && !resolveSeatNow(static_cast<uint8_t>(module), static_cast<uint8_t>(slot), snapshot))) {
    sendJson(server, 404, "{\"ok\":false,\"error\":\"Seat is not available\"}");
    return;
  }
  const uint8_t controllerId = static_cast<uint8_t>(module);
  const uint8_t seatSlot = static_cast<uint8_t>(slot);

  if (!requester && profileIdForPhysicalSeat(controllerId, seatSlot).length() == 0) {
    sendJson(server, 409, "{\"ok\":false,\"error\":\"This seat is a guest. Create or sign into an account, then attach the Sigil from the lobby\"}");
    return;
  }

  // PIN-free physical play is not permission to obtain a browser credential.
  // Profiles without a PIN retain the physical bootstrap path to set one.
  if (!requester && hasPin(controllerId, seatSlot)) {
    sendError(server, 403, "Sign in with this profile's PIN to use it in a browser");
    return;
  }

  const uint32_t nowMs = millis();
  cleanup(nowMs);

  // A newer request for the same seat replaces the older one.
  for (auto &pending : pendingClaims) {
    if (pending.used && pending.controllerId == controllerId && pending.slot == seatSlot) {
      pending = PendingClaim{};
    }
  }

  PendingClaim *target = nullptr;
  for (auto &pending : pendingClaims) {
    if (!pending.used) {
      target = &pending;
      break;
    }
  }
  if (target == nullptr) {
    sendJson(server, 503, "{\"ok\":false,\"error\":\"Too many pending claims\"}");
    return;
  }

  uint64_t requestId = random64();
  if (requestId == 0) requestId = 1;  // 0 means "invalid" to parseRequestId.

  *target = PendingClaim{};
  target->used = true;
  target->requestId = requestId;
  target->controllerId = controllerId;
  target->slot = seatSlot;
  target->createdMs = nowMs;
  const String claimedProfile = profileIdForPhysicalSeat(controllerId, seatSlot);
  strncpy(target->profileId, claimedProfile.c_str(), sizeof(target->profileId) - 1);
  if (requester) strncpy(target->requestingToken, requester->token, sizeof(target->requestingToken) - 1);

  String response = "{\"ok\":true,\"status\":\"pending\",\"requestId\":\"";
  response += requestIdText(requestId);
  response += "\",\"module\":";
  response += String(module);
  response += ",\"slot\":";
  response += String(slot);
  response += ",\"expiresMs\":";
  response += String(CLAIM_TIMEOUT_MS);
  response += ",\"message\":\"On this Sigil, choose Link phone within ";
  response += String(CLAIM_TIMEOUT_MS / 1000);
  response += " seconds to authorize this seat.\"}";
  sendJson(server, 202, response);
}

// Collects a claim result once. Approved claims hand over the token.
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

  cleanup(millis());
  for (auto &pending : pendingClaims) {
    if (!pending.used || pending.requestId != id) continue;

    if (!pending.approved) {
      sendJson(server, 200, "{\"ok\":true,\"status\":\"pending\"}");
      return;
    }
    if (pending.error[0]) {
      const String error(pending.error);
      pending = PendingClaim{};
      sendError(server, 409, error);
      return;
    }
    String response = "{\"ok\":true,\"status\":\"approved\",\"module\":";
    response += String(pending.controllerId);
    response += ",\"slot\":";
    response += String(pending.slot);
    response += ",\"token\":\"";
    response += pending.token;
    response += "\"}";
    pending = PendingClaim{};
    sendJson(server, 200, response);
    return;
  }
  sendJson(server, 404, "{\"ok\":false,\"status\":\"expired\",\"error\":\"Claim expired or was already collected\"}");
}

// profileId + PIN signs into a profile; module + slot + PIN signs into the
// profile bound to that Sigil seat.
void handleSessionLogin(WebServer &server) {
  if (server.hasArg("profileId")) {
    handleProfileLogin(server);
  } else {
    handleSeatLogin(server);
  }
}

void handleSessionMe(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"authenticated\":false}");
    return;
  }

  SeatSnapshot snapshot;
  const bool participating = resolveSessionParticipant(*session) &&
      resolveSeatNow(session->controllerId, session->slot, snapshot);
  const SigilRecord *record = recordForModule(session->controllerId);
  const String profileId = sessionProfileId(*session);
  TurnHubAccounts::Account account;
  const bool accountReady = TurnHubAccounts::load(profileId, account);

  String response = "{\"ok\":true,\"authenticated\":true,\"module\":";
  response += String(session->controllerId);
  response += ",\"permissions\":"; response += String(accountReady ? account.permissions : 0);
  response += ",\"participating\":"; response += jsonBool(participating);
  response += ",\"host\":"; response += jsonBool(snapshot.host);
  response += ",\"virtual\":"; response += jsonBool(session->controllerId >= MAX_PHYSICAL_SIGILS);
  response += ",\"slot\":"; response += String(session->slot);
  response += ",\"slotName\":\""; response += session->slot == 1 ? "A" : "B";
  response += "\",\"player\":"; response += String(snapshot.playerNumber);
  response += ",\"active\":"; response += jsonBool(snapshot.active);
  response += ",\"eliminated\":"; response += jsonBool(snapshot.eliminated);
  response += ",\"lifeAvailable\":"; response += jsonBool(snapshot.lifeAvailable);
  response += ",\"life\":"; response += String(snapshot.life);
  response += ",\"profileId\":\""; response += jsonEscape(profileId);
  response += "\",\"statsUrl\":\"/stats\",\"name\":\"";
  response += jsonEscape(TurnHubProfiles::nameForProfile(profileId));
  response += "\",\"hasPin\":"; response += jsonBool(TurnHubProfiles::hasPinForProfile(profileId));
  TurnHubProfiles::ProfilePolicy policy;
  const bool policyAvailable = TurnHubProfiles::loadPolicyForProfile(profileId, policy);
  response += ",\"policyAvailable\":"; response += jsonBool(policyAvailable);
  response += ",\"tablet\":"; response += jsonBool(session->tableDevice);
  if (policyAvailable) {
    response += ",\"allowPhysicalWithoutPin\":"; response += jsonBool(policy.allowPhysicalWithoutPin);
    response += ",\"hideStatsWithoutAuthentication\":";
    response += jsonBool(policy.hideStatsWithoutAuthentication);
  }
  if (record != nullptr) {
    response += ",\"hardwareId\":\"";
    response += sigilHardwareId(record->mac);
    response += '"';
  }
  response += '}';
  sendJson(server, 200, response);
}

// Venue tables: follow another game. A profile in a lobby leaves it first;
// one playing in a game finishes (or the table is reset) before moving. A
// table tablet just moves; its seated players stay where they are.
void handleSessionGame(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendError(server, 401, "Sign in to a profile first");
    return;
  }
  const long game = server.arg("game").toInt();
  if (game < 1 || game > tableHooks.count) {
    sendError(server, 400, "No such game");
    return;
  }
  const uint8_t target = static_cast<uint8_t>(game - 1);
  const String profileId = sessionProfileId(*session);
  if (!session->tableDevice && tableHooks.profileTable != nullptr) {
    const int8_t seated = tableHooks.profileTable(profileId);
    // This request already acts on the game the profile is seated in.
    String message;
    if (seated >= 0 && seated != target &&
        (!profileControlHandler ||
         !profileControlHandler(profileId, WebControl::Leave, INVALID_ID, 1, message))) {
      sendError(server, 409, "Finish this game before you switch");
      return;
    }
  }
  session->table = target;
  sendOkMessage(server, String("Following Game ") + String(game));
}

void handleLogout(WebServer &server) {
  const String token = server.header(TOKEN_HEADER);
  for (auto &session : sessions) {
    if (session.used && token.equalsIgnoreCase(session.token)) {
      session = WebSession{};
      break;
    }
  }
  sendJson(server, 200, "{\"ok\":true}");
}

}  // namespace internal

// --- Public API -------------------------------------------------------------------

using namespace internal;

bool hasPendingClaim(uint8_t sigilId) {
  const uint32_t nowMs = millis();
  for (const auto &pending : pendingClaims) {
    if (pending.used && !pending.approved && pending.controllerId == sigilId &&
        nowMs - pending.createdMs <= CLAIM_TIMEOUT_MS) return true;
  }
  return false;
}

// Called when Link phone is chosen on the Sigil: approves the oldest pending claim for that
// Sigil, attaching the requesting browser's profile or issuing a new session.
void notePhysicalAction(uint8_t sigilId) {
  const uint32_t nowMs = millis();
  cleanup(nowMs);

  PendingClaim *oldest = nullptr;
  for (auto &pending : pendingClaims) {
    if (!pending.used || pending.approved || pending.controllerId != sigilId) continue;
    if (oldest == nullptr || pending.createdMs < oldest->createdMs) oldest = &pending;
  }
  if (oldest == nullptr) return;

  const bool attaching = oldest->requestingToken[0] != '\0';
  WebSession *requester = attaching ? sessionForToken(String(oldest->requestingToken), nowMs) : nullptr;
  if (!attaching &&
      (hasPin(oldest->controllerId, oldest->slot) ||
       profileIdForPhysicalSeat(oldest->controllerId, oldest->slot) != oldest->profileId)) {
    oldest->approved = true;
    strncpy(oldest->error, "Profile changed or requires PIN login; sign in again", sizeof(oldest->error) - 1);
    return;
  }
  if (attaching) {
    String message = "Sign-in expired; request attachment again";
    if (!requester || !profileControlHandler ||
        !profileControlHandler(sessionProfileId(*requester), WebControl::AttachPhysical,
            oldest->controllerId, oldest->slot, message)) {
      oldest->approved = true;
      strncpy(oldest->error, message.c_str(), sizeof(oldest->error) - 1);
      return;
    }
  }
  SeatSnapshot snapshot;
  if (!resolveSeatNow(oldest->controllerId, oldest->slot, snapshot)) {
    *oldest = PendingClaim{};
    return;
  }

  // A Sigil press proves possession, not knowledge of a PIN.
  WebSession *session = requester ? requester :
      createSession(oldest->controllerId, oldest->slot, nowMs, false);
  if (session == nullptr) return;

  oldest->approved = true;
  strncpy(oldest->token, session->token, sizeof(oldest->token) - 1);
  oldest->token[sizeof(oldest->token) - 1] = '\0';

  serialLog.print("ATLAS|WEB_SESSION|AUTHORIZED|SIGIL|");
  serialLog.print(oldest->controllerId);
  serialLog.print("|SLOT|");
  serialLog.print(oldest->slot == 1 ? 'A' : 'B');
  serialLog.print("|PROFILE|");
  serialLog.println(session->profileId);
}

bool profileAuthenticated(const String &profileId) {
  cleanup(millis());
  if (profileId.length() == 0) return false;
  for (const auto &session : sessions) {
    if (session.used && profileId == session.profileId) return true;
  }
  return false;
}

bool physicalUseAllowed(const String &profileId) {
  // Guest participation needs no durable identity. Bound accounts still use
  // their saved authentication and moderation policy below.
  if (profileId.length() == 0) return true;
  if (connectionBlocked(profileId)) return false;
  TurnHubProfiles::ProfilePolicy policy;
  return TurnHubProfiles::loadPolicyForProfile(profileId, policy) &&
      (policy.allowPhysicalWithoutPin || profileAuthenticated(profileId));
}

bool physicalStatsVisible(const String &profileId) {
  TurnHubProfiles::ProfilePolicy policy;
  return TurnHubProfiles::loadPolicyForProfile(profileId, policy) &&
      (!policy.hideStatsWithoutAuthentication || profileAuthenticated(profileId));
}

// Archived accounts and accounts reset by moderation must sign in again.
bool connectionBlocked(const String &id) {
  TurnHubAccounts::Account account;
  return TurnHubAccounts::load(id, account) && (account.archived || account.reconnectRequired);
}

// Ends every session and pending claim belonging to the profile.
void revokeConnections(const String &id) {
  for (auto &session : sessions) {
    if (!session.used || id != session.profileId) continue;
    for (auto &pending : pendingClaims) {
      if (pending.used && !strcmp(pending.requestingToken, session.token)) pending = PendingClaim{};
    }
    session = WebSession{};
  }
  for (auto &pending : pendingClaims) {
    if (pending.used && id == pending.profileId) pending = PendingClaim{};
  }
}

bool hasPermission(WebServer &server, uint8_t permission) {
  WebSession *session = sessionForRequest(server);
  return session != nullptr && TurnHubAccounts::has(String(session->profileId), permission);
}

bool requirePermission(WebServer &server, uint8_t permission) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return false;
  }
  if (!TurnHubAccounts::has(String(session->profileId), permission)) {
    sendError(server, 403, "Account permission required");
    return false;
  }
  return true;
}

bool verifiedAtTable(WebServer &server) { return internal::physicalPresence(server); }
}  // namespace TurnHubWebApi
