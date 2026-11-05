// Browser authentication: session tokens, PIN hashing and verification,
// profile sign-in and the account policy queries.

#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include "nvs_blob_store.h"
#include <string.h>

#include "account_access.h"
#include "login_limiter.h"
#include "serial_log.h"
#include "web_api_internal.h"
#include "wall_clock.h"

using TurnHub::serialLog;

namespace TurnHubWebApi {
namespace internal {

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
  if (pin.length() == 0 && TurnHubProfiles::profileExists(id)) {
    if (connectionBlocked(id)) { sendError(server,403,"Profile is unavailable for play"); return; }
    sendLogin(server, createProfileSession(id, millis(), false));
    return;
  }
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

String sessionProfileId(const WebSession &session) {
  const String profileId(session.profileId);
  return TurnHubProfiles::profileExists(profileId) ? profileId : String();
}

// --- PINs and names ---------------------------------------------------------------

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

namespace {

String linkKeyHash(const String &profileId, const String &key) {
  String material = "TurnHubLink:";
  material += profileId;
  material += ':';
  material += key;
  return sha256Hex(material).substring(0, TurnHubProfiles::LINK_KEY_HASH_LENGTH);
}
}  // namespace

String issueLinkKey(const String &profileId) {
  TurnHubProfiles::ProfileSync sync;
  if (!TurnHubProfiles::loadSyncForProfile(profileId, sync)) return String();
  const String key = requestIdText(random64()) + requestIdText(random64());
  const String hash = linkKeyHash(profileId, key);
  if (hash.length() != TurnHubProfiles::LINK_KEY_HASH_LENGTH) return String();
  // The oldest of the two phones' keys gives way.
  memcpy(sync.keyHashes[sync.nextKey], hash.c_str(), TurnHubProfiles::LINK_KEY_HASH_LENGTH);
  sync.nextKey = static_cast<uint8_t>((sync.nextKey + 1) % TurnHubProfiles::MAX_LINK_KEYS);
  return TurnHubProfiles::saveSyncForProfile(profileId, sync) ? key : String();
}

bool linkKeyValid(const String &profileId, const String &key) {
  TurnHubProfiles::ProfileSync sync;
  if (key.length() != 2 * REQUEST_ID_LENGTH || !TurnHubProfiles::loadSyncForProfile(profileId, sync)) return false;
  const String hash = linkKeyHash(profileId, key);
  bool found = false;
  for (uint8_t k = 0; k < TurnHubProfiles::MAX_LINK_KEYS; ++k)
    found = found || (sync.keyHashes[k][0] != 0 && hash == String(sync.keyHashes[k]));
  return found;
}

void observeWallClock(uint64_t epochMs) {
  TurnHubClock::clock.observe(epochMs, millis());
}

uint64_t wallClockNow() {
  return TurnHubClock::clock.now(millis());
}

void noteProfileChange(const String &profileId) {
  TurnHubProfiles::ProfileSync sync;
  if (!TurnHubProfiles::loadSyncForProfile(profileId, sync)) return;
  const uint64_t now = wallClockNow();
  if (now) {
    sync.activityMs = now;
    sync.changedWithoutClock = false;
  } else {
    sync.changedWithoutClock = true;
  }
  TurnHubProfiles::saveSyncForProfile(profileId, sync);
}

String cleanName(const String &raw) {
  String name = raw;
  name.trim();
  if (name.length() > MAX_NAME_LENGTH) name.remove(MAX_NAME_LENGTH);
  return name;
}

void handleSessionLogin(WebServer &server) {
  handleProfileLogin(server);
}

void handleSessionMe(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (session == nullptr) {
    sendJson(server, 401, "{\"ok\":false,\"authenticated\":false}");
    return;
  }

  const String profileId = sessionProfileId(*session);
  TurnHubAccounts::Account account;
  const bool accountReady = TurnHubAccounts::load(profileId, account);

  String response = "{\"ok\":true,\"authenticated\":true";
  response += ",\"permissions\":"; response += String(accountReady && session->pinVerified ? account.permissions : 0);
  response += ",\"profileId\":\""; response += jsonEscape(profileId);
  response += "\",\"statsUrl\":\"/stats\",\"name\":\"";
  response += jsonEscape(TurnHubProfiles::nameForProfile(profileId));
  response += "\",\"hasPin\":"; response += jsonBool(TurnHubProfiles::hasPinForProfile(profileId));
  TurnHubProfiles::ProfilePolicy policy;
  const bool policyAvailable = TurnHubProfiles::loadPolicyForProfile(profileId, policy);
  response += ",\"policyAvailable\":"; response += jsonBool(policyAvailable);
  if (policyAvailable) {
    response += ",\"allowPhysicalWithoutPin\":"; response += jsonBool(policy.allowPhysicalWithoutPin);
    response += ",\"hideStatsWithoutAuthentication\":";
    response += jsonBool(policy.hideStatsWithoutAuthentication);
  }
  response += '}';
  sendJson(server, 200, response);
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

bool profileAuthenticated(const String &profileId) {
  cleanup(millis());
  if (profileId.length() == 0) return false;
  for (const auto &session : sessions) {
    if (session.used && session.pinVerified && profileId == session.profileId) return true;
  }
  return false;
}

bool physicalUseAllowed(const String &profileId) {
  // Guest participation needs no durable identity. Bound accounts still use
  // their saved authentication and moderation policy below.
  if (profileId.length() == 0) return true;
  if (connectionBlocked(profileId)) return false;
  return TurnHubProfiles::profileExists(profileId);
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

// Ends every session belonging to the profile.
void revokeConnections(const String &id) {
  for (auto &session : sessions) {
    if (!session.used || id != session.profileId) continue;
    session = WebSession{};
  }
}

bool hasPermission(WebServer &server, uint8_t permission) {
  WebSession *session = sessionForRequest(server);
  return session != nullptr && session->pinVerified && TurnHubAccounts::has(String(session->profileId), permission);
}

bool requirePermission(WebServer &server, uint8_t permission) {
  WebSession *session = sessionForRequest(server);
  if (!session) {
    sendError(server, 401, "Sign in first");
    return false;
  }
  if (!session->pinVerified || !TurnHubAccounts::has(String(session->profileId), permission)) {
    sendError(server, 403, "Account permission required");
    return false;
  }
  return true;
}

bool verifiedAtTable(WebServer &server) { return internal::physicalPresence(server); }

}  // namespace TurnHubWebApi
