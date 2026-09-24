// Atlas front panel: the master button (the on-board BOOT button, a hardware
// adapter that only dispatches Intents and proves physical presence) and the
// pairing window timer. The E32R28T board has no Pair button or front-panel
// LEDs; its RGB LED and speaker amplifier are parked off.

#include "atlas_app.h"
#include "config.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

bool pairingActive = false;

namespace {

constexpr uint32_t DEBOUNCE_MS = 25;

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
  masterButton.lastState = digitalRead(AtlasConfig::MASTER_BUTTON_PIN);

  // Park the on-board peripherals Atlas does not drive yet: RGB LED off
  // (active low) and the speaker amplifier disabled (enable is active low).
  pinMode(AtlasConfig::RGB_RED_PIN, OUTPUT);
  pinMode(AtlasConfig::RGB_GREEN_PIN, OUTPUT);
  pinMode(AtlasConfig::RGB_BLUE_PIN, OUTPUT);
  digitalWrite(AtlasConfig::RGB_RED_PIN, HIGH);
  digitalWrite(AtlasConfig::RGB_GREEN_PIN, HIGH);
  digitalWrite(AtlasConfig::RGB_BLUE_PIN, HIGH);
  pinMode(AtlasConfig::AUDIO_ENABLE_PIN, OUTPUT);
  digitalWrite(AtlasConfig::AUDIO_ENABLE_PIN, HIGH);
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

// The pairing window closes after its configured length or when the lobby ends.
void updatePairingWindow(uint32_t nowMs) {
  if (pairingActive &&
      (nowMs - pairingStartedAtMs >= pairingIndicatorMs || hubState != HubState::Lobby)) {
    pairingActive = false;
    serialLog.println("ATLAS|PAIRING|EXIT");
  }
}

}  // namespace TurnHubAtlas
