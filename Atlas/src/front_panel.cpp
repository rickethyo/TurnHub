// Atlas front panel state: the pairing window timer, the table presence codes
// and the board's BOOT button. The E32R28T board has no other buttons; the
// touchscreen (touch_controls.cpp) is Atlas's main physical input. A code the
// Atlas screen shows, typed or scanned on a phone, proves that phone's user is
// at the table, which protected web actions (first Admin, system settings,
// device names, OTA, Return to lobby, factory reset) require.

#include <esp_system.h>
#include <string.h>

#include "atlas_app.h"
#include "config.h"
#include "serial_log.h"
#include "three_part_button.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

bool pairingActive = false;

namespace {

uint32_t pairingStartedAtMs = 0;
uint32_t pairingIndicatorMs = TurnHubProtocol::PAIRING_WINDOW_MS;

// The on-board RGB LED's red channel as last written (active low).
bool pairingLedLit = false;

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

// While Atlas's pairing window is open, its on-board LED blinks red in the
// same rhythm as a pairing Sigil (250 ms on, 250 ms off). The Atlas screen
// says so in words too; the light is an extra cue, never the only one.
static bool pairingLedOn(uint32_t elapsedMs) { return elapsedMs % 500 < 250; }

static void setPairingLed(bool lit) {
  if (lit == pairingLedLit) return;
  pairingLedLit = lit;
  digitalWrite(AtlasConfig::RGB_RED_PIN, lit ? LOW : HIGH);
}

void beginFrontPanel() {
  // Park the on-board RGB LED (active low); pairing blinks its red channel.
  pinMode(AtlasConfig::RGB_RED_PIN, OUTPUT);
  pinMode(AtlasConfig::RGB_GREEN_PIN, OUTPUT);
  pinMode(AtlasConfig::RGB_BLUE_PIN, OUTPUT);
  digitalWrite(AtlasConfig::RGB_RED_PIN, HIGH);
  digitalWrite(AtlasConfig::RGB_GREEN_PIN, HIGH);
  digitalWrite(AtlasConfig::RGB_BLUE_PIN, HIGH);
  pinMode(AtlasConfig::BOOT_BUTTON_PIN, INPUT_PULLUP);
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

namespace {

constexpr uint32_t BOOT_BUTTON_DEBOUNCE_MS = 30;
bool bootRaw = false;
bool bootStable = false;
uint32_t bootChangedMs = 0;
TurnHubProtocol::ThreePartButton bootGesture;

void dispatchBootButtonIntent(IntentType type, int32_t value, const char *gesture) {
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::AtlasHardware;
  intent.payload.value = value;
  const IntentResult result = intents.dispatch(intent);
  serialLog.print("ATLAS|BOOT_BUTTON|");
  serialLog.print(gesture);
  serialLog.print(result.accepted() ? "|OK|" : "|REJECTED|");
  serialLog.println(result.message);
}

}  // namespace

// Adapter: builds Intents only. The handlers decide what each gesture may do.
void updateBootButton(bool pressed, uint32_t nowMs) {
  if (pressed != bootRaw) {
    bootRaw = pressed;
    bootChangedMs = nowMs;
  }
  if (nowMs - bootChangedMs >= BOOT_BUTTON_DEBOUNCE_MS) bootStable = bootRaw;
  switch (bootGesture.update(bootStable, nowMs)) {
    case TurnHubProtocol::ButtonGesture::Pair:
      dispatchBootButtonIntent(IntentType::PairRequest, 0, "PAIR");
      break;
    case TurnHubProtocol::ButtonGesture::Unpair:
      audio.play(TurnHub::AudioCue::ActionRequired, TurnHub::AudioController::ATLAS_SPEAKER_MASK);
      dispatchBootButtonIntent(IntentType::ForgetPairing, TurnHub::FORGET_ALL_SIGILS, "UNPAIR");
      break;
    case TurnHubProtocol::ButtonGesture::FactoryReset:
      audio.play(TurnHub::AudioCue::TimerExpired, TurnHub::AudioController::ATLAS_SPEAKER_MASK);
      dispatchBootButtonIntent(IntentType::FactoryReset, TurnHub::FACTORY_RESET_ATLAS, "FACTORY_RESET");
      break;
    case TurnHubProtocol::ButtonGesture::None:
      break;
  }
}

// The pairing window closes after its configured length or when the lobby
// ends; a shown presence code closes after PRESENCE_CODE_MS.
void updatePairingWindow(uint32_t nowMs) {
  if (pairingActive &&
      (nowMs - pairingStartedAtMs >= pairingIndicatorMs || hubState != HubState::Lobby)) {
    pairingActive = false;
    serialLog.println("ATLAS|PAIRING|EXIT");
  }
  setPairingLed(pairingActive && pairingLedOn(nowMs - pairingStartedAtMs));
  if (codeShown && !codeLive(nowMs)) {
    codeShown = false;
    serialLog.println("ATLAS|PRESENCE|CODE_EXPIRED");
  }
}

}  // namespace TurnHubAtlas
