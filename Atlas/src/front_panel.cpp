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
// A match-time master hold blinks the status LED fast from this point on, so
// the person holding it can see a hold is counting toward ending the match.
constexpr uint32_t END_MATCH_WARNING_MS = 1000;
constexpr uint32_t END_MATCH_BLINK_INTERVAL_MS = 100;

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
uint32_t pairingIndicatorMs = TurnHubProtocol::PAIRING_WINDOW_MS;

// Master hold that started during a match; endMatchSent suppresses the
// release PASS once the hold has asked to end the match.
bool masterHeldInMatch = false;
bool endMatchSent = false;
uint32_t masterPressedAtMs = 0;

bool matchInProgress() {
  return hubState == HubState::Running || hubState == HubState::Paused;
}

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
  pairingIndicatorMs = pairingWindowMs;
}

bool otaAllowed() {
  const bool safeState = hubState == HubState::Lobby || hubState == HubState::GameOver;
  return safeState && AtlasConfig::masterButtonPressed();
}

// Releasing the master button passes (or cancels a queued pass) for the
// active player, like a PASS press on that player's Sigil. Holding it for
// MASTER_END_MATCH_HOLD_MS during a match ends the match as a draw instead.
void updateMasterButton() {
  bool state;
  if (!masterButton.changed(state)) {
    if (!masterHeldInMatch || endMatchSent ||
        millis() - masterPressedAtMs < MASTER_END_MATCH_HOLD_MS) return;
    endMatchSent = true;
    Intent intent;
    intent.type = IntentType::EndMatch;
    intent.actor.origin = IntentOrigin::AtlasHardware;
    const IntentResult result = intents.dispatch(intent);
    serialLog.print("ATLAS|MASTER_BUTTON|END_MATCH|");
    serialLog.println(result.accepted() ? "ACCEPTED" : result.message);
    return;
  }
  if (state == LOW) {
    masterHeldInMatch = matchInProgress();
    endMatchSent = false;
    masterPressedAtMs = millis();
    return;
  }
  const bool heldToEnd = endMatchSent;
  masterHeldInMatch = false;
  endMatchSent = false;
  if (heldToEnd || hubState != HubState::Running) return;

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
  const uint32_t heldMs = nowMs - masterPressedAtMs;
  const bool endMatchWarning = masterHeldInMatch && !endMatchSent &&
      matchInProgress() && heldMs >= END_MATCH_WARNING_MS;
  bool statusOn = !bootBlinkActive || (bootElapsed / BOOT_BLINK_INTERVAL_MS) % 2 == 0;
  if (endMatchWarning) statusOn = (heldMs / END_MATCH_BLINK_INTERVAL_MS) % 2 == 0;
  digitalWrite(AtlasConfig::STATUS_LED_PIN, statusOn ? HIGH : LOW);

  // The Pair LED is reserved for the pairing window, which leaving the lobby ends.
  const uint32_t pairingElapsed = nowMs - pairingStartedAtMs;
  if (pairingActive &&
      (pairingElapsed >= pairingIndicatorMs || hubState != HubState::Lobby)) {
    pairingActive = false;
    serialLog.println("ATLAS|PAIRING|EXIT");
  }
  digitalWrite(AtlasConfig::PAIR_LED_PIN,
      pairingActive && (pairingElapsed / PAIR_BLINK_INTERVAL_MS) % 2 == 0 ? HIGH : LOW);
}

}  // namespace TurnHubAtlas
