// Atlas front panel state: the pairing window timer and the table presence
// codes. The E32R28T board has no master, Pair or front-panel buttons; the
// touchscreen (touch_controls.cpp) is Atlas's only physical input. A code the
// Atlas screen shows, typed or scanned on a phone, proves that phone's user is
// at the table, which protected web actions (first Admin, system settings,
// device names, OTA, Return to lobby, factory reset) require.

#include <esp_system.h>
#include <string.h>

#include "atlas_app.h"
#include "config.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

bool pairingActive = false;

namespace {

uint32_t pairingStartedAtMs = 0;
uint32_t pairingIndicatorMs = TurnHubProtocol::PAIRING_WINDOW_MS;

bool codeShown = false;
PresenceRequest shownCode;
uint8_t wrongAttempts = 0;

struct PresenceGrant {
  char profileId[9] = {};
  uint32_t grantedAtMs = 0;
  bool used = false;
};
PresenceGrant grants[PRESENCE_MAX_GRANTS];

bool codeLive(uint32_t nowMs) {
  return codeShown && nowMs - shownCode.shownAtMs < PRESENCE_CODE_MS;
}

bool grantLive(const PresenceGrant &grant, uint32_t nowMs) {
  return grant.used && nowMs - grant.grantedAtMs < PRESENCE_GRANT_MS;
}

PresenceGrant *grantFor(const String &profileId) {
  for (auto &grant : grants) {
    if (grant.used && profileId == grant.profileId) return &grant;
  }
  return nullptr;
}

}  // namespace

void beginFrontPanel() {
  // Park the on-board RGB LED, which Atlas does not drive yet (active low).
  pinMode(AtlasConfig::RGB_RED_PIN, OUTPUT);
  pinMode(AtlasConfig::RGB_GREEN_PIN, OUTPUT);
  pinMode(AtlasConfig::RGB_BLUE_PIN, OUTPUT);
  digitalWrite(AtlasConfig::RGB_RED_PIN, HIGH);
  digitalWrite(AtlasConfig::RGB_GREEN_PIN, HIGH);
  digitalWrite(AtlasConfig::RGB_BLUE_PIN, HIGH);
}

void startPairingIndicator(uint32_t nowMs) {
  pairingActive = true;
  pairingStartedAtMs = nowMs;
  pairingIndicatorMs = pairingWindowMs;
}

bool requestPresenceCode(const String &profileId, bool setup, uint32_t nowMs) {
  if (profileId.length() != 8) return false;
  shownCode = PresenceRequest();
  strncpy(shownCode.profileId, profileId.c_str(), sizeof(shownCode.profileId) - 1);
  shownCode.code = 100000 + esp_random() % 900000;
  shownCode.shownAtMs = nowMs;
  shownCode.setup = setup;
  codeShown = true;
  wrongAttempts = 0;
  // The code itself is never logged: it is the proof.
  serialLog.print("ATLAS|PRESENCE|CODE_SHOWN|");
  serialLog.println(setup ? "SETUP" : "ADMIN");
  return true;
}

PresenceOutcome confirmPresenceCode(const String &profileId, uint32_t code, uint32_t nowMs) {
  if (!codeLive(nowMs) || profileId != shownCode.profileId) return PresenceOutcome::NoCode;
  if (code != shownCode.code) {
    if (++wrongAttempts >= PRESENCE_MAX_ATTEMPTS) {
      codeShown = false;
      serialLog.println("ATLAS|PRESENCE|TOO_MANY_ATTEMPTS");
      return PresenceOutcome::TooManyAttempts;
    }
    return PresenceOutcome::WrongCode;
  }
  codeShown = false;
  PresenceGrant *grant = grantFor(profileId);
  if (grant == nullptr) {
    // Reuse a free or expired slot, else the oldest.
    grant = &grants[0];
    for (auto &candidate : grants) {
      if (!grantLive(candidate, nowMs)) { grant = &candidate; break; }
      if (static_cast<int32_t>(candidate.grantedAtMs - grant->grantedAtMs) < 0) grant = &candidate;
    }
  }
  *grant = PresenceGrant();
  strncpy(grant->profileId, profileId.c_str(), sizeof(grant->profileId) - 1);
  grant->grantedAtMs = nowMs;
  grant->used = true;
  serialLog.print("ATLAS|PRESENCE|VERIFIED|");
  serialLog.println(PRESENCE_GRANT_MS);
  return PresenceOutcome::Verified;
}

const PresenceRequest *pendingPresenceCode(uint32_t nowMs) {
  return codeLive(nowMs) ? &shownCode : nullptr;
}

void cancelPresenceCode() {
  if (!codeShown) return;
  codeShown = false;
  serialLog.println("ATLAS|PRESENCE|CODE_CANCELLED");
}

uint32_t presenceRemainingMs(const String &profileId, uint32_t nowMs) {
  const PresenceGrant *grant = grantFor(profileId);
  if (grant == nullptr || !grantLive(*grant, nowMs)) return 0;
  return PRESENCE_GRANT_MS - (nowMs - grant->grantedAtMs);
}

bool presenceConfirmedFor(const String &profileId, uint32_t nowMs) {
  return presenceRemainingMs(profileId, nowMs) > 0;
}

bool anyPresenceActive(uint32_t nowMs) {
  for (const auto &grant : grants) if (grantLive(grant, nowMs)) return true;
  return false;
}

void revokePresence(const String &profileId) {
  PresenceGrant *grant = grantFor(profileId);
  if (grant == nullptr) return;
  *grant = PresenceGrant();
  serialLog.println("ATLAS|PRESENCE|REVOKED");
}

void resetPresence() {
  codeShown = false;
  wrongAttempts = 0;
  for (auto &grant : grants) grant = PresenceGrant();
}

bool otaAllowed() {
  return hubState == HubState::Lobby || hubState == HubState::GameOver;
}

uint32_t pairingRemainingMs(uint32_t nowMs) {
  if (!pairingActive) return 0;
  const uint32_t elapsed = nowMs - pairingStartedAtMs;
  return elapsed < pairingIndicatorMs ? pairingIndicatorMs - elapsed : 0;
}

// The pairing window closes after its configured length or when the lobby
// ends; a shown presence code closes after PRESENCE_CODE_MS.
void updatePairingWindow(uint32_t nowMs) {
  if (pairingActive &&
      (nowMs - pairingStartedAtMs >= pairingIndicatorMs || hubState != HubState::Lobby)) {
    pairingActive = false;
    serialLog.println("ATLAS|PAIRING|EXIT");
  }
  if (codeShown && !codeLive(nowMs)) {
    codeShown = false;
    serialLog.println("ATLAS|PRESENCE|CODE_EXPIRED");
  }
}

}  // namespace TurnHubAtlas
