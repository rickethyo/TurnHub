// Atlas front panel: the master and Pair buttons (hardware adapters that only
// dispatch Intents) and the status/Pair indicator LEDs.

#include "atlas_app.h"
#include "config.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

bool pairingActive = false;

namespace {

constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t BOOT_BLINK_INTERVAL_MS = 150;
constexpr uint32_t BOOT_BLINK_PHASES = 6;  // Three on/off flashes.
constexpr uint32_t PAIR_BLINK_INTERVAL_MS = 250;

// Edge detector for an active-low INPUT_PULLUP button.
struct DebouncedButton {
  uint8_t pin;
  const char *logPrefix;
  bool lastState = HIGH;
  uint32_t lastChangeMs = 0;

  // A constructor, not brace aggregate init: the ESP32 toolchain builds as
  // C++11, where default member initializers make this a non-aggregate.
  DebouncedButton(uint8_t buttonPin, const char *prefix) : pin(buttonPin), logPrefix(prefix) {}

  // Returns true once per debounced edge and reports the new level.
  bool changed(bool &state) {
    const bool current = digitalRead(pin);
    if (current == lastState || millis() - lastChangeMs < DEBOUNCE_MS) return false;
    lastChangeMs = millis();
    lastState = current;
    state = current;
    serialLog.print(logPrefix);
    serialLog.println(current == LOW ? "|DOWN" : "|UP");
    return true;
  }
};

DebouncedButton masterButton{AtlasConfig::MASTER_BUTTON_PIN, "ATLAS|MASTER_BUTTON"};
DebouncedButton pairButton{AtlasConfig::PAIR_BUTTON_PIN, "ATLAS|PAIR_BUTTON"};

bool bootBlinkActive = false;
uint32_t bootBlinkStartedAtMs = 0;
uint32_t pairingStartedAtMs = 0;

}  // namespace

void beginFrontPanel() {
  pinMode(AtlasConfig::MASTER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(AtlasConfig::PAIR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(AtlasConfig::STATUS_LED_PIN, OUTPUT);
  pinMode(AtlasConfig::PAIR_LED_PIN, OUTPUT);

  masterButton.lastState = digitalRead(AtlasConfig::MASTER_BUTTON_PIN);
  pairButton.lastState = digitalRead(AtlasConfig::PAIR_BUTTON_PIN);

  digitalWrite(AtlasConfig::STATUS_LED_PIN, HIGH);
  digitalWrite(AtlasConfig::PAIR_LED_PIN, LOW);
}

void startBootBlink(uint32_t nowMs) {
  bootBlinkStartedAtMs = nowMs;
  bootBlinkActive = true;
}

void startPairingIndicator(uint32_t nowMs) {
  pairingActive = true;
  pairingStartedAtMs = nowMs;
}

bool otaAllowed() {
  const bool safeState = hubState == HubState::Lobby || hubState == HubState::GameOver;
  return safeState && AtlasConfig::masterButtonPressed();
}

// Releasing the master button passes (or cancels a queued pass) for the
// active player, like a PASS press on that player's Sigil.
void updateMasterButton() {
  bool state;
  if (!masterButton.changed(state)) return;
  if (state != HIGH || hubState != HubState::Running) return;

  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr) return;
  const IntentResult result = dispatchSeatIntent(IntentType::Pass, IntentOrigin::AtlasHardware, *active);
  if (!result.accepted()) {
    serialLog.print("ATLAS|MASTER_BUTTON|PASS_REJECTED|");
    serialLog.println(result.message);
  } else if (pendingPass.active && pendingPass.seat.sameSeat(*active)) {
    serialLog.println("ATLAS|MASTER_BUTTON|PASS_PENDING");
  } else {
    serialLog.println("ATLAS|MASTER_BUTTON|PASS_CANCELLED");
  }
}

void updatePairButton() {
  bool state;
  if (!pairButton.changed(state) || state != LOW) return;
  Intent intent;
  intent.type = IntentType::PairRequest;
  intent.actor.origin = IntentOrigin::AtlasHardware;
  intents.dispatch(intent);
}

void updateFrontPanelLeds(uint32_t nowMs) {
  // Three status flashes after boot, then steady on.
  const uint32_t bootElapsed = nowMs - bootBlinkStartedAtMs;
  if (bootBlinkActive && bootElapsed >= BOOT_BLINK_PHASES * BOOT_BLINK_INTERVAL_MS) {
    bootBlinkActive = false;
  }
  digitalWrite(AtlasConfig::STATUS_LED_PIN,
      !bootBlinkActive || (bootElapsed / BOOT_BLINK_INTERVAL_MS) % 2 == 0 ? HIGH : LOW);

  // The Pair LED is reserved for the pairing window, which leaving the lobby ends.
  const uint32_t pairingElapsed = nowMs - pairingStartedAtMs;
  if (pairingActive &&
      (pairingElapsed >= TurnHubProtocol::PAIRING_WINDOW_MS || hubState != HubState::Lobby)) {
    pairingActive = false;
    serialLog.println("ATLAS|PAIRING|EXIT");
  }
  digitalWrite(AtlasConfig::PAIR_LED_PIN,
      pairingActive && (pairingElapsed / PAIR_BLINK_INTERVAL_MS) % 2 == 0 ? HIGH : LOW);
}

}  // namespace TurnHubAtlas
