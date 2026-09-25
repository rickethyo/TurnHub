// Atlas front panel state: the pairing window timer and the admin unlock
// window. The E32R28T board has no master, Pair or front-panel buttons; the
// touchscreen (touch_controls.cpp) is Atlas's only physical input. Holding
// "Unlock admin" there proves someone is at the table, which protected web
// actions (first Admin, system settings, device names, OTA) require.

#include "atlas_app.h"
#include "config.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

bool pairingActive = false;

namespace {

uint32_t pairingStartedAtMs = 0;
uint32_t pairingIndicatorMs = TurnHubProtocol::PAIRING_WINDOW_MS;

bool adminUnlocked = false;
uint32_t adminUnlockedAtMs = 0;

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

void openAdminUnlock(uint32_t nowMs) {
  adminUnlocked = true;
  adminUnlockedAtMs = nowMs;
  serialLog.print("ATLAS|ADMIN_UNLOCK|OPEN|");
  serialLog.println(ADMIN_UNLOCK_WINDOW_MS);
}

void closeAdminUnlock() {
  if (!adminUnlocked) return;
  adminUnlocked = false;
  serialLog.println("ATLAS|ADMIN_UNLOCK|CLOSED");
}

uint32_t adminUnlockRemainingMs(uint32_t nowMs) {
  if (!adminUnlocked) return 0;
  const uint32_t elapsed = nowMs - adminUnlockedAtMs;
  return elapsed < ADMIN_UNLOCK_WINDOW_MS ? ADMIN_UNLOCK_WINDOW_MS - elapsed : 0;
}

bool physicalPresenceConfirmed() {
  return adminUnlockRemainingMs(millis()) > 0;
}

bool otaAllowed() {
  const bool safeState = hubState == HubState::Lobby || hubState == HubState::GameOver;
  return safeState && physicalPresenceConfirmed();
}

uint32_t pairingRemainingMs(uint32_t nowMs) {
  if (!pairingActive) return 0;
  const uint32_t elapsed = nowMs - pairingStartedAtMs;
  return elapsed < pairingIndicatorMs ? pairingIndicatorMs - elapsed : 0;
}

// The pairing window closes after its configured length or when the lobby
// ends; the admin unlock closes after ADMIN_UNLOCK_WINDOW_MS.
void updatePairingWindow(uint32_t nowMs) {
  if (pairingActive &&
      (nowMs - pairingStartedAtMs >= pairingIndicatorMs || hubState != HubState::Lobby)) {
    pairingActive = false;
    serialLog.println("ATLAS|PAIRING|EXIT");
  }
  if (adminUnlocked && adminUnlockRemainingMs(nowMs) == 0) {
    adminUnlocked = false;
    serialLog.println("ATLAS|ADMIN_UNLOCK|EXPIRED");
  }
}

}  // namespace TurnHubAtlas
