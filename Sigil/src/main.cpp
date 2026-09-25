// Sigil player controller firmware. Buttons become ESP-NOW packets for
// Atlas; Atlas's packets drive the LEDs, buzzer and selected display. Sigil
// decides nothing about the game (ARCHITECTURAL_INVARIANTS.md, Invariant 1):
// it only persists the minimum pairing binding needed to find Atlas again.
//
// Threads: ESP-NOW receive callbacks only enqueue packets; loop() handles
// them. Rendering runs on its own task (displayTask) because e-paper refresh
// blocks for seconds; state shared with it is guarded by displayProfileMux.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <freertos/queue.h>

#include "firmware_version.h"
#include "protocol.h"
#include "sigil_display.h"
#include "sigil_led.h"
#include "received_packet.h"

#ifndef TURNHUB_INPUT_JOYSTICK
#define TURNHUB_INPUT_JOYSTICK 0
#endif
#if TURNHUB_INPUT_JOYSTICK
#include "joystick_input.h"
#endif
#ifndef TURNHUB_STATUS_RING
#define TURNHUB_STATUS_RING 0
#endif
#if TURNHUB_STATUS_RING
#if !TURNHUB_INPUT_JOYSTICK
#error "The status ring uses GPIO26, which is the Pass button in button builds"
#endif
#include "status_ring.h"
#endif

namespace {

using TurnHubProtocol::DisplayMode;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;
using TurnHubSigil::SigilDisplay;

constexpr uint8_t UNASSIGNED_SIGIL_ID = 0xFF;
constexpr uint8_t WIFI_CHANNEL = 6;

// Status LED: one RGB LED (or three single LEDs), PWM on every channel.
constexpr uint8_t BLUE_LED = 27;
constexpr uint8_t GREEN_LED = 14;
constexpr uint8_t RED_LED = 13;
#if TURNHUB_STATUS_RING
// NeoPixel Jewel 7 Data Input, via 330 ohm (J10). Powered from 5V (J1).
constexpr uint8_t STATUS_RING_PIN = 26;
#endif
#if TURNHUB_INPUT_JOYSTICK
// Analog thumbstick in place of the three buttons (stick powered from 3.3 V,
// never 5 V: VRX/VRY swing to the supply). Clicking the stick is PASS;
// pushing right is Action and down is Pause/Win, with the same tap and hold
// timing as the buttons. VRX/VRY must be ADC1 pins: ADC2 is unusable while
// ESP-NOW has the radio. GPIO25 is unused; GPIO26 drives the status ring.
constexpr uint8_t PASS_BUTTON = 32;        // SW, switch to GND (J13).
constexpr uint8_t JOYSTICK_X_PIN = 34;     // VRX, input-only ADC1 (J15).
constexpr uint8_t JOYSTICK_Y_PIN = 35;     // VRY, input-only ADC1 (J14).
constexpr uint8_t JOYSTICK_CALIBRATION_SAMPLES = 16;
constexpr uint32_t JOYSTICK_SAMPLE_MS = 5;
// Virtual pins: never GPIO numbers, read from the stick direction instead.
constexpr uint8_t ACTION_BUTTON = 0xF0;
constexpr uint8_t PAUSE_WIN_BUTTON = 0xF1;
constexpr TurnHubSigil::StickDirection ACTION_DIRECTION = TurnHubSigil::StickDirection::Right;
constexpr TurnHubSigil::StickDirection PAUSE_WIN_DIRECTION = TurnHubSigil::StickDirection::Down;
#else
constexpr uint8_t PASS_BUTTON = 26;
constexpr uint8_t ACTION_BUTTON = 25;
constexpr uint8_t PAUSE_WIN_BUTTON = 32; // Breadboard J13; switch to GND.
#endif
constexpr uint8_t BUZZER_PIN = 33;
constexpr uint8_t BUZZER_CHANNEL = 7;
#if defined(TURNHUB_WOKWI)
constexpr uint8_t PAIR_BUTTON = 19;  // diagram.json's Pair pushbutton.
#else
// The DevKit's onboard BOOT button (GPIO0, pulled up on the board). GPIO0 is
// a strapping pin only at reset: holding BOOT while resetting enters the ROM
// downloader instead of this firmware, so Pair works from boot onward.
constexpr uint8_t PAIR_BUTTON = 0;
#endif

// Both display implementations consume the same existing display packets.
// The OLED build also says so, so Atlas limits it to one player.
constexpr uint8_t DEVICE_CAPABILITIES =
    TurnHubProtocol::CAPABILITY_DISPLAY |
    TurnHubProtocol::CAPABILITY_DISPLAY_PROFILE |
    TurnHubProtocol::CAPABILITY_GAME_DISPLAY |
    TurnHubProtocol::CAPABILITY_INPUT_TIMING |
    TurnHubProtocol::CAPABILITY_LED_STATE
#if TURNHUB_DISPLAY_OLED
    | TurnHubProtocol::CAPABILITY_DISPLAY_OLED
#endif
    ;

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t HELLO_INTERVAL_MS = 2000;
constexpr uint32_t PAIRING_DURATION_MS = TurnHubProtocol::PAIRING_WINDOW_MS;
// Status light: frames are rendered at most this often.
constexpr uint32_t LED_FRAME_MS = 20;
constexpr uint32_t DISPLAY_TASK_STACK_BYTES = 4096;
constexpr uint32_t PROFILE_REQUEST_RETRY_MS = 1000;
constexpr uint8_t PROFILE_REQUEST_MAX_ATTEMPTS = 4;
constexpr uint8_t PROFILE_SLOT_MASK = 0x03;
constexpr uint8_t RECEIVE_QUEUE_LENGTH = 32;

// NVS pairing binding: Atlas MAC (6 bytes) followed by the assigned Sigil ID.
constexpr char PAIRING_NAMESPACE[] = "th_pair_v1";
constexpr char PAIRING_KEY[] = "atlas";
constexpr size_t PAIRING_BINDING_SIZE = 7;

constexpr uint8_t BROADCAST_MAC[6] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Active-low INPUT_PULLUP button with debounce and hold tracking.
struct ButtonState {
  explicit ButtonState(uint8_t buttonPin) : pin(buttonPin) {}

  uint8_t pin;
  bool rawState = HIGH;
  bool stableState = HIGH;
  uint32_t lastDebounceMs = 0;
  uint32_t pressStartMs = 0;
  bool longSent = false;
  bool winSent = false;
};

ButtonState passButton(PASS_BUTTON);
ButtonState actionButton(ACTION_BUTTON);
ButtonState pauseWinButton(PAUSE_WIN_BUTTON);
ButtonState pairButton(PAIR_BUTTON);
SigilDisplay &sigilDisplay = TurnHubSigil::getSigilDisplay();

bool espNowReady = false;
bool atlasKnown = false;
uint8_t atlasMac[6] = {};
volatile uint8_t sigilId = UNASSIGNED_SIGIL_ID;
uint32_t lastHelloMs = 0;
uint32_t buzzerStopAtMs = 0;
// Status light: Atlas's LedState (or legacy channels) plus Sigil-local
// pairing and pass-ack, rendered to the RGB LED pins and the Jewel ring.
TurnHubSigil::SigilLedModel ledModel;
uint32_t lastLedFrameMs = 0;
bool ledOutputValid = false;
TurnHubSigil::Rgb shownSingle;
volatile bool pairingActive = false;
uint32_t pairingStartMs = 0;
int32_t pairingToken = 0;
uint32_t lastPairRequestMs = 0;
// Hold thresholds: Atlas sends them from the seated players' accessibility
// preferences (InputTiming). Runtime only; a reboot returns to the defaults
// until Atlas resends them.
uint32_t longPressMs = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
uint32_t winHoldMs = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;
using TurnHubSigil::ReceivedPacket;
QueueHandle_t receiveQueue = nullptr;
volatile bool displayNeedsRefresh = false;
volatile int32_t displayPayload = 0;
uint8_t displayRenderedSigilId = UNASSIGNED_SIGIL_ID;
TaskHandle_t displayTaskHandle = nullptr;

TurnHubProtocol::GameDisplayPacket pendingGameDisplay{};
bool gameDisplayValid = false;
portMUX_TYPE displayProfileMux = portMUX_INITIALIZER_UNLOCKED;
char pendingSeatNames[2][TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
uint16_t receivedNameChunks[2] = {};
uint8_t finalNameChunk[2] = {0xFF, 0xFF};
volatile uint8_t pendingSeatNameMask = 0;
volatile uint8_t completedProfileSlotMask = 0;
volatile bool profileSyncStartPending = false;
bool profileRequestActive = false;
uint8_t profileRequestAttempts = 0;
uint32_t lastProfileRequestMs = 0;

void printMac(const uint8_t *mac) {
  Serial.printf(
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;

  const esp_err_t result = esp_now_add_peer(&peer);
  if (result != ESP_OK) {
    Serial.print("SIGIL|ESP_NOW|PEER_ERROR|");
    printMac(mac);
    Serial.print("|");
    Serial.println(static_cast<int>(result));
    return false;
  }

  return true;
}

void rememberAtlas(const uint8_t *mac) {
  if (!atlasKnown || memcmp(atlasMac, mac, 6) != 0) {
    memcpy(atlasMac, mac, 6);
    atlasKnown = true;

    Serial.print("SIGIL|ATLAS|DISCOVERED|");
    printMac(atlasMac);
    Serial.println();
  }

  ensurePeer(atlasMac);
}

void sendPacket(
    PacketType type,
    int32_t value = 0,
    bool forceBroadcast = false) {
  if (!espNowReady || (type != PacketType::PairRequest && !atlasKnown)) {
    return;
  }

  const uint8_t *destination =
      (forceBroadcast || !atlasKnown) ? BROADCAST_MAC : atlasMac;

  if (!ensurePeer(destination)) {
    return;
  }

  const Packet packet = TurnHubProtocol::makePacket(type, sigilId, value);
  const esp_err_t result = esp_now_send(
      destination,
      reinterpret_cast<const uint8_t *>(&packet),
      sizeof(packet));

  if (result != ESP_OK) {
    Serial.print("SIGIL|ESP_NOW|SEND_ERROR|");
    Serial.println(static_cast<int>(result));
  }
}

void sendHello() {
  if (!atlasKnown) return;
  const int32_t helloInfo = TurnHubProtocol::encodeHelloInfo(
      TurnHubSigilFirmware::MAJOR,
      TurnHubSigilFirmware::MINOR,
      TurnHubSigilFirmware::PATCH,
      DEVICE_CAPABILITIES);
  sendPacket(PacketType::Hello, helloInfo);
  lastHelloMs = millis();
}

// Renders the current light state: the RGB LED pins (PWM on all three, so a
// single RGB LED shows full color) and, on the E-ink build, the Jewel ring.
// Both only change hardware when the frame differs.
void updateLeds() {
  const uint32_t nowMs = millis();
  if (ledOutputValid && nowMs - lastLedFrameMs < LED_FRAME_MS) return;
  lastLedFrameMs = nowMs;
  const TurnHubSigil::LedFrame frame = ledModel.render(nowMs);
  if (!ledOutputValid || frame.single != shownSingle) {
    analogWrite(RED_LED, frame.single.r);
    analogWrite(GREEN_LED, frame.single.g);
    analogWrite(BLUE_LED, frame.single.b);
    shownSingle = frame.single;
  }
  ledOutputValid = true;
#if TURNHUB_STATUS_RING
  TurnHubSigil::statusRingShow(frame);
#endif
}

#if TURNHUB_INPUT_JOYSTICK
// Both axes read reversed as the stick is mounted on the E-ink Sigil
// (owner-verified 2026-09-25), so push right/down read as left/up raw.
TurnHubSigil::StickConfig stickConfig() {
  TurnHubSigil::StickConfig config;
  config.invertX = true;
  config.invertY = true;
  return config;
}
TurnHubSigil::StickTracker stick(stickConfig());
uint32_t lastStickSampleMs = 0;

// Rest the stick at boot: a missing or held stick disables the directions
// (the click still works as PASS).
void calibrateJoystick() {
  int16_t xs[JOYSTICK_CALIBRATION_SAMPLES];
  int16_t ys[JOYSTICK_CALIBRATION_SAMPLES];
  for (uint8_t i = 0; i < JOYSTICK_CALIBRATION_SAMPLES; ++i) {
    xs[i] = static_cast<int16_t>(analogRead(JOYSTICK_X_PIN));
    ys[i] = static_cast<int16_t>(analogRead(JOYSTICK_Y_PIN));
    delay(2);
  }
  const bool ok = stick.calibrate(xs, ys, JOYSTICK_CALIBRATION_SAMPLES);
  Serial.printf("SIGIL|JOYSTICK|%s|CENTER|%d|%d\n", ok ? "READY" : "NOT_CENTERED_DISABLED",
      stick.centerX(), stick.centerY());
}

void updateJoystick(uint32_t nowMs) {
  if (nowMs - lastStickSampleMs < JOYSTICK_SAMPLE_MS) return;
  lastStickSampleMs = nowMs;
  const TurnHubSigil::StickDirection before = stick.direction();
  const TurnHubSigil::StickDirection after = stick.update(
      static_cast<int16_t>(analogRead(JOYSTICK_X_PIN)),
      static_cast<int16_t>(analogRead(JOYSTICK_Y_PIN)));
  if (after != before) {
    Serial.print("SIGIL|JOYSTICK|");
    Serial.println(TurnHubSigil::stickDirectionName(after));
  }
}
#endif

// Active-low reading; joystick virtual pins are LOW while pushed that way.
bool readButton(uint8_t pin) {
#if TURNHUB_INPUT_JOYSTICK
  if (pin == ACTION_BUTTON) return stick.direction() == ACTION_DIRECTION ? LOW : HIGH;
  if (pin == PAUSE_WIN_BUTTON) return stick.direction() == PAUSE_WIN_DIRECTION ? LOW : HIGH;
#endif
  return digitalRead(pin);
}

// Returns true once each time the button settles in a new state.
bool debouncedEdge(ButtonState &button, uint32_t nowMs) {
  const bool reading = readButton(button.pin);
  if (reading != button.rawState) {
    button.rawState = reading;
    button.lastDebounceMs = nowMs;
  }
  if (nowMs - button.lastDebounceMs < DEBOUNCE_MS || button.stableState == button.rawState) {
    return false;
  }
  button.stableState = button.rawState;
  return true;
}

void notifyDisplayTask() {
  if (displayTaskHandle != nullptr) {
    xTaskNotifyGive(displayTaskHandle);
  }
}

void queueReadyDisplay() {
  portENTER_CRITICAL(&displayProfileMux);
  gameDisplayValid = false;
  portEXIT_CRITICAL(&displayProfileMux);
  displayPayload = TurnHubProtocol::encodeDisplayState(
      DisplayMode::Ready, 0, 0, 0);
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

void startDisplayProfileSync() {
  portENTER_CRITICAL(&displayProfileMux);
  memset(pendingSeatNames, 0, sizeof(pendingSeatNames));
  memset(receivedNameChunks, 0, sizeof(receivedNameChunks));
  finalNameChunk[0] = 0xFF;
  finalNameChunk[1] = 0xFF;
  pendingSeatNameMask = 0;
  completedProfileSlotMask = 0;
  portEXIT_CRITICAL(&displayProfileMux);

  profileRequestActive = true;
  profileRequestAttempts = 0;
  lastProfileRequestMs = 0;
}

bool applyPendingSeatNames() {
  char localNames[2][TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  uint8_t mask = 0;

  portENTER_CRITICAL(&displayProfileMux);
  mask = pendingSeatNameMask;
  for (uint8_t index = 0; index < 2; ++index) {
    if ((mask & (1u << index)) != 0) {
      memcpy(localNames[index], pendingSeatNames[index], sizeof(localNames[index]));
    }
  }
  pendingSeatNameMask = 0;
  portEXIT_CRITICAL(&displayProfileMux);

  bool changed = false;
  if ((mask & 0x01u) != 0) {
    changed |= sigilDisplay.setSeatName(1, localNames[0]);
  }
  if ((mask & 0x02u) != 0) {
    changed |= sigilDisplay.setSeatName(2, localNames[1]);
  }

  return changed;
}

void handleDisplayNameChunk(int32_t value) {
  const uint8_t slot = TurnHubProtocol::displayNameSlot(value);
  if (slot != 1 && slot != 2) {
    return;
  }

  const uint8_t chunkIndex = TurnHubProtocol::displayNameChunkIndex(value);
  const uint8_t offset =
      chunkIndex * TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS;
  if (offset >= TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH) {
    return;
  }

  const uint8_t index = slot - 1;
  bool completed = false;

  portENTER_CRITICAL(&displayProfileMux);
  char *target = pendingSeatNames[index];

  if (chunkIndex == 0) {
    memset(target, 0, TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1);
    receivedNameChunks[index] = 0;
    finalNameChunk[index] = 0xFF;
  }

  for (uint8_t i = 0; i < TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS; ++i) {
    const uint8_t targetIndex = offset + i;
    if (targetIndex >= TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH) {
      break;
    }
    target[targetIndex] = TurnHubProtocol::displayNameChar(value, i);
  }
  target[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH] = '\0';

  receivedNameChunks[index] |= static_cast<uint16_t>(1u << chunkIndex);
  if (TurnHubProtocol::displayNameFinalChunk(value)) {
    finalNameChunk[index] = chunkIndex;
  }

  if (finalNameChunk[index] != 0xFF) {
    const uint16_t expectedMask = static_cast<uint16_t>(
        (1u << (finalNameChunk[index] + 1u)) - 1u);
    if ((receivedNameChunks[index] & expectedMask) == expectedMask) {
      pendingSeatNameMask |= static_cast<uint8_t>(1u << index);
      completedProfileSlotMask |= static_cast<uint8_t>(1u << index);
      completed = true;
    }
  }
  portEXIT_CRITICAL(&displayProfileMux);

  if (completed) {
    Serial.print("SIGIL|DISPLAY_PROFILE|SEAT|");
    Serial.print(slot == 1 ? 'A' : 'B');
    Serial.println("|READY");
    // The display worker compares completed names before requesting a refresh.
    notifyDisplayTask();
  }
}

void updateDisplayProfileSync() {
  if (profileSyncStartPending) {
    profileSyncStartPending = false;
    startDisplayProfileSync();
  }

  if (!profileRequestActive) {
    return;
  }

  uint8_t completedMask = 0;
  portENTER_CRITICAL(&displayProfileMux);
  completedMask = completedProfileSlotMask;
  portEXIT_CRITICAL(&displayProfileMux);

  if ((completedMask & PROFILE_SLOT_MASK) == PROFILE_SLOT_MASK) {
    profileRequestActive = false;
    Serial.println("SIGIL|DISPLAY_PROFILE|SYNC|COMPLETE");
    return;
  }

  if (!espNowReady || !atlasKnown || sigilId == UNASSIGNED_SIGIL_ID) {
    return;
  }

  const uint32_t nowMs = millis();
  if (
      profileRequestAttempts != 0 &&
      nowMs - lastProfileRequestMs < PROFILE_REQUEST_RETRY_MS) {
    return;
  }

  if (profileRequestAttempts >= PROFILE_REQUEST_MAX_ATTEMPTS) {
    profileRequestActive = false;
    Serial.print("SIGIL|DISPLAY_PROFILE|SYNC|INCOMPLETE|MASK|0x");
    Serial.println(completedMask, HEX);
    return;
  }

  sendPacket(PacketType::DisplayProfileRequest);
  ++profileRequestAttempts;
  lastProfileRequestMs = nowMs;
  Serial.print("SIGIL|DISPLAY_PROFILE|REQUEST|");
  Serial.println(profileRequestAttempts);
}

void updateDisplay() {
  static TurnHubProtocol::GameDisplayPacket renderedGame{};
  static bool renderedGameValid = false;
  TurnHubProtocol::GameDisplayPacket currentGame{};
  portENTER_CRITICAL(&displayProfileMux);
  const bool hasGame = gameDisplayValid;
  if (hasGame) currentGame = pendingGameDisplay;
  portEXIT_CRITICAL(&displayProfileMux);
  if (hasGame && currentGame.sigilId == sigilId) {
    applyPendingSeatNames();
    // Clear before drawing; packets arriving while the panel is busy wake us again.
    displayNeedsRefresh = false;
    if (!renderedGameValid || memcmp(&renderedGame, &currentGame, sizeof(currentGame))) {
      sigilDisplay.showGame(currentGame);
      renderedGame = currentGame;
      renderedGameValid = true;
    }
    displayRenderedSigilId = sigilId;
    return;
  }
  renderedGameValid = false;
  if (applyPendingSeatNames()) {
    displayNeedsRefresh = true;
  }

  const uint8_t currentSigilId = sigilId;

  if (
      currentSigilId != UNASSIGNED_SIGIL_ID &&
      displayRenderedSigilId != currentSigilId) {
    displayRenderedSigilId = currentSigilId;
    displayPayload = TurnHubProtocol::encodeDisplayState(
        DisplayMode::Ready, 0, 0, 0);
    displayNeedsRefresh = false;
    Serial.print("SIGIL|DISPLAY|ASSIGNED|");
    Serial.println(currentSigilId);
    sigilDisplay.showReady(currentSigilId);
    return;
  }

  if (!displayNeedsRefresh) {
    return;
  }

  displayNeedsRefresh = false;

  if (currentSigilId == UNASSIGNED_SIGIL_ID) {
    displayRenderedSigilId = UNASSIGNED_SIGIL_ID;
    sigilDisplay.showUnpaired();
    return;
  }

  const int32_t payload = displayPayload;
  const DisplayMode mode = TurnHubProtocol::displayMode(payload);

  Serial.print("SIGIL|DISPLAY|STATE|");
  Serial.print(currentSigilId);
  Serial.print("|");
  Serial.println(static_cast<unsigned>(mode));

  if (mode == DisplayMode::Ready) {
    sigilDisplay.showReady(currentSigilId);
    return;
  }

  sigilDisplay.showState(
      currentSigilId,
      mode,
      TurnHubProtocol::displayPrimaryPlayer(payload),
      TurnHubProtocol::displaySecondaryPlayer(payload),
      TurnHubProtocol::displayTurnNumber(payload),
      TurnHubProtocol::displayFlags(payload));
}

void displayTask(void *parameter) {
  (void)parameter;
  Serial.println("SIGIL|DISPLAY|TASK|READY");

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    updateDisplay();

    // A newer display packet may have arrived while the e-paper was busy.
    // Render the newest state immediately instead of waiting for another event.
    if (displayNeedsRefresh) {
      xTaskNotifyGive(displayTaskHandle);
    }
  }
}

void stopBuzzer() {
  ledcWriteTone(BUZZER_CHANNEL, 0);
  buzzerStopAtMs = 0;
}

void playBuzzerPayload(int32_t value) {
  const uint16_t frequencyHz = TurnHubProtocol::toneFrequency(value);
  const uint16_t durationMs = TurnHubProtocol::toneDuration(value);

  if (frequencyHz == 0 || durationMs == 0) {
    stopBuzzer();
    return;
  }

  ledcWriteTone(BUZZER_CHANNEL, frequencyHz);
  buzzerStopAtMs = millis() + durationMs;
  Serial.print("SIGIL|BUZZER|");
  Serial.print(frequencyHz);
  Serial.print("|");
  Serial.println(durationMs);
}

void updateBuzzer() {
  if (buzzerStopAtMs == 0) {
    return;
  }

  if (static_cast<int32_t>(millis() - buzzerStopAtMs) >= 0) {
    stopBuzzer();
  }
}

// Erases the saved Atlas pairing and returns to the unpaired screen, from a
// long Pair hold or Atlas's Unpair. Atlas keeps its own record until an admin
// forgets this Sigil there (which also sends Unpair if the Sigil is in range).
void forgetPairing(const char *reason) {
  Preferences prefs;
  bool erased = false;
  if (prefs.begin(PAIRING_NAMESPACE, false)) {
    erased = !prefs.isKey(PAIRING_KEY) || prefs.remove(PAIRING_KEY);
    prefs.end();
  }
  if (!erased) {
    Serial.println("SIGIL|PAIR|FORGET|STORE_ERROR");
    return;
  }
  if (atlasKnown && esp_now_is_peer_exist(atlasMac)) esp_now_del_peer(atlasMac);
  atlasKnown = false;
  memset(atlasMac, 0, sizeof(atlasMac));
  sigilId = UNASSIGNED_SIGIL_ID;
  pairingActive = false;
  profileRequestActive = false;
  longPressMs = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  winHoldMs = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;
  ledModel.clear();
  ledModel.setPairing(false, millis());
  stopBuzzer();
  portENTER_CRITICAL(&displayProfileMux);
  gameDisplayValid = false;
  portEXIT_CRITICAL(&displayProfileMux);
  displayNeedsRefresh = true;
  notifyDisplayTask();
  Serial.print("SIGIL|PAIR|FORGOTTEN|");
  Serial.println(reason);
}

void handleEspNowReceive(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
  if (length == sizeof(TurnHubProtocol::GameDisplayPacket)) {
    TurnHubProtocol::GameDisplayPacket snapshot{};
    memcpy(&snapshot, incomingData, sizeof(snapshot));
    if (!atlasKnown || memcmp(mac, atlasMac, 6) || snapshot.sigilId != sigilId ||
        !TurnHubProtocol::validGameDisplay(snapshot) ||
        TurnHubProtocol::displayMode(snapshot.state) != DisplayMode::Running) return;
    portENTER_CRITICAL(&displayProfileMux);
    const bool changed = !gameDisplayValid || memcmp(&snapshot, &pendingGameDisplay, sizeof(snapshot));
    pendingGameDisplay = snapshot;
    gameDisplayValid = true;
    portEXIT_CRITICAL(&displayProfileMux);
    if (changed) { displayNeedsRefresh = true; notifyDisplayTask(); }
    return;
  }
  if (length != sizeof(Packet)) {
    return;
  }

  Packet packet{};
  memcpy(&packet, incomingData, sizeof(packet));

  if (packet.version != TurnHubProtocol::VERSION) {
    return;
  }

  if (packet.type == PacketType::PairAccept) {
    if (!pairingActive || millis() - pairingStartMs >= PAIRING_DURATION_MS ||
        packet.value != pairingToken || packet.sigilId >= TurnHubProtocol::MAX_SIGILS) return;
    uint8_t binding[PAIRING_BINDING_SIZE];
    memcpy(binding, mac, 6);
    binding[6] = packet.sigilId;
    Preferences prefs;
    if (!prefs.begin(PAIRING_NAMESPACE, false)) return;
    const bool stored = prefs.putBytes(PAIRING_KEY, binding, sizeof(binding)) == sizeof(binding);
    prefs.end();
    if (!stored) { Serial.println("SIGIL|PAIR|STORE_ERROR"); return; }
    rememberAtlas(mac);
    sigilId = packet.sigilId;
    pairingActive = false;
    ledModel.setPairing(false, millis());
    profileSyncStartPending = true;
    queueReadyDisplay();
    Serial.println("SIGIL|PAIR|SUCCESS");
    sendHello();
    return;
  }
  if (!atlasKnown || memcmp(mac, atlasMac, 6) != 0) return;

  if (packet.type == PacketType::Ack &&
      packet.value == static_cast<int32_t>(PacketType::Hello)) {
    if (packet.sigilId >= TurnHubProtocol::MAX_SIGILS) return;

    if (sigilId != packet.sigilId) {
      sigilId = packet.sigilId;
      profileSyncStartPending = true;
      queueReadyDisplay();
      Serial.print("SIGIL|ID|");
      Serial.println(sigilId);
    }

    Serial.println("SIGIL|ACK|HELLO");
    return;
  }

  if (sigilId == UNASSIGNED_SIGIL_ID || packet.sigilId != sigilId) {
    return;
  }

  rememberAtlas(mac);

  switch (packet.type) {
    case PacketType::Ack:
      Serial.print("SIGIL|ACK|");
      Serial.println(packet.value);

      if (packet.value == static_cast<int32_t>(PacketType::Pass)) {
        ledModel.flashPassAck(millis());
      }
      break;

    case PacketType::LedState:
      ledModel.applyLedState(packet.value, millis());
      break;

    // Legacy channel stream from an Atlas that predates LedState.
    case PacketType::SetBlue:
      ledModel.applyLegacyBlue(static_cast<uint8_t>(constrain(packet.value, 0, 255)));
      break;

    case PacketType::SetRed:
      ledModel.applyLegacyRed(packet.value != 0);
      break;

    case PacketType::SetGreen:
      ledModel.applyLegacyGreen(packet.value != 0);
      break;

    case PacketType::Buzzer:
      playBuzzerPayload(packet.value);
      break;

    case PacketType::InputTiming: {
      const uint16_t longMs = TurnHubProtocol::inputTimingLongPress(packet.value);
      const uint16_t winMs = TurnHubProtocol::inputTimingWinHold(packet.value);
      if (!TurnHubProtocol::validInputTiming(longMs, winMs)) break;
      if (longMs != longPressMs || winMs != winHoldMs) {
        longPressMs = longMs;
        winHoldMs = winMs;
        Serial.print("SIGIL|INPUT_TIMING|");
        Serial.print(longPressMs);
        Serial.print("|");
        Serial.println(winHoldMs);
      }
      break;
    }

    case PacketType::DisplayProfileRequest:
      profileSyncStartPending = true;
      break;

    case PacketType::Unpair:
      // Only the saved Atlas, addressing this Sigil's ID, gets here.
      forgetPairing("ATLAS");
      break;

    case PacketType::DisplayNameChunk:
      handleDisplayNameChunk(packet.value);
      break;

    case PacketType::DisplayState: {
      portENTER_CRITICAL(&displayProfileMux);
      const bool wasGame = gameDisplayValid;
      gameDisplayValid = false;
      portEXIT_CRITICAL(&displayProfileMux);
      if (wasGame || displayPayload != packet.value) {
        displayPayload = packet.value;
        displayNeedsRefresh = true;
        notifyDisplayTask();
      }
      break;
    }

    default:
      break;
  }
}

// PASS is sent on release.
void updatePassButton() {
  if (!debouncedEdge(passButton, millis())) return;
  if (passButton.stableState == HIGH) {
    Serial.print("SIGIL|");
    Serial.print(sigilId);
    Serial.println("|PASS");
    sendPacket(PacketType::Pass);
  }
}

// Only a physical Pair press enables broadcast association requests.
void startPairing() {
  if (pairingActive) {
    return;
  }

  pairingToken = static_cast<int32_t>(esp_random());
  lastPairRequestMs = millis() - HELLO_INTERVAL_MS;
  pairingStartMs = millis();
  pairingActive = true;
  ledModel.setPairing(true, pairingStartMs);
  Serial.print("SIGIL|PAIR|START|DURATION_MS|");
  Serial.println(PAIRING_DURATION_MS);
}

void updatePairing() {
  if (!pairingActive) {
    return;
  }

  const uint32_t elapsedMs = millis() - pairingStartMs;
  if (elapsedMs >= PAIRING_DURATION_MS) {
    pairingActive = false;
    ledModel.setPairing(false, millis());
    Serial.println("SIGIL|PAIR|TIMEOUT");
    return;
  }

  if (millis() - lastPairRequestMs >= HELLO_INTERVAL_MS) {
    sendPacket(PacketType::PairRequest, pairingToken, true);
    lastPairRequestMs = millis();
  }
}

// A press opens the pairing window; keeping Pair held for
// FORGET_PAIRING_HOLD_MS (10 s) erases the saved pairing instead.
void updatePairButton() {
  const uint32_t nowMs = millis();
  if (debouncedEdge(pairButton, nowMs)) {
    if (pairButton.stableState == LOW) {
      pairButton.pressStartMs = nowMs;
      pairButton.longSent = false;
      Serial.println("SIGIL|PAIR|BUTTON");
      startPairing();
    } else {
      pairButton.pressStartMs = 0;
      Serial.println("SIGIL|PAIR|BUTTON|UP");
    }
    return;
  }
  if (pairButton.stableState == LOW && pairButton.pressStartMs != 0 && !pairButton.longSent &&
      nowMs - pairButton.pressStartMs >= TurnHubProtocol::FORGET_PAIRING_HOLD_MS) {
    pairButton.longSent = true;
    forgetPairing("BUTTON");
  }
}

void sendAction(PacketType type, const char *name) {
  Serial.print("SIGIL|");
  Serial.print(sigilId);
  Serial.print("|");
  Serial.println(name);
  sendPacket(type);
}

// Action: Down on press, then Long at longPressMs (default 2 s) and Win at
// winHoldMs (default 5 s) while held; Up on release, preceded by Short if no
// Long was sent. The dedicated Pause/Win button (pauseWin) sends Long on a tap
// and waits winHoldMs before Long + Win.
void updateActionButton(ButtonState &actionButton, bool pauseWin = false) {
  const uint32_t nowMs = millis();
  if (debouncedEdge(actionButton, nowMs)) {
    if (actionButton.stableState == LOW) {
      actionButton.pressStartMs = nowMs;
      actionButton.longSent = false;
      actionButton.winSent = false;
      sendAction(PacketType::ActionDown, "ACTION_DOWN");
    } else {
      // The dedicated Pause / Win tap uses the existing long-action semantic.
      if (pauseWin && !actionButton.longSent) {
        sendAction(PacketType::ActionLong, "ACTION_LONG");
      }
      sendAction(PacketType::ActionUp, "ACTION_UP");

      if (!pauseWin && !actionButton.longSent) {
        sendAction(PacketType::ActionShort, "ACTION_SHORT");
      }

      actionButton.pressStartMs = 0;
      actionButton.longSent = false;
      actionButton.winSent = false;
    }
  }

  if (actionButton.stableState != LOW || actionButton.pressStartMs == 0) {
    return;
  }

  const uint32_t heldMs = nowMs - actionButton.pressStartMs;

  // A dedicated win hold arms the claim immediately before sending it,
  // rather than pausing at the original Action button's 2-second threshold.
  if (!actionButton.longSent && heldMs >= (pauseWin ? winHoldMs : longPressMs)) {
    actionButton.longSent = true;
    sendAction(PacketType::ActionLong, "ACTION_LONG");
  }

  if (!actionButton.winSent && heldMs >= winHoldMs) {
    actionButton.winSent = true;
    sendAction(PacketType::ActionWin, "ACTION_WIN");
  }
}

void loadSavedPairing() {
  Preferences prefs;
  if (prefs.begin(PAIRING_NAMESPACE, false)) {
    uint8_t binding[PAIRING_BINDING_SIZE];
    if (prefs.isKey(PAIRING_KEY) && prefs.getBytesLength(PAIRING_KEY) == sizeof(binding) &&
        prefs.getBytes(PAIRING_KEY, binding, sizeof(binding)) == sizeof(binding) &&
        binding[6] < TurnHubProtocol::MAX_SIGILS) {
      // Read before the first screen, but register the peer only after ESP-NOW
      // starts. A saved binding remains paired even if radio startup fails.
      memcpy(atlasMac, binding, sizeof(atlasMac));
      atlasKnown = true;
      sigilId = binding[6];
      Serial.println("SIGIL|PAIR|LOADED");
    }
    prefs.end();
  }
}

bool startEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  esp_wifi_set_promiscuous(true);
  const esp_err_t channelResult =
      esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (channelResult != ESP_OK) {
    Serial.print("SIGIL|WIFI_CHANNEL|ERROR|");
    Serial.println(static_cast<int>(channelResult));
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("SIGIL|ESP_NOW|ERROR");
    return false;
  }

  receiveQueue = xQueueCreate(RECEIVE_QUEUE_LENGTH, sizeof(ReceivedPacket));
  if (!receiveQueue) return false;
  esp_now_register_recv_cb([](const uint8_t *mac, const uint8_t *data, int length) {
    ReceivedPacket received{};
    if (!received.assign(mac, data, length)) return;
    xQueueSend(receiveQueue, &received, 0);
  });
  if (atlasKnown) {
    rememberAtlas(atlasMac);
    profileSyncStartPending = true;
    queueReadyDisplay();
  }

  if (!ensurePeer(BROADCAST_MAC)) {
    return false;
  }

  Serial.print("SIGIL|MAC|");
  Serial.println(WiFi.macAddress());
  Serial.print("SIGIL|FW|");
  Serial.println(TurnHubSigilFirmware::VERSION);
  Serial.println("SIGIL|ESP_NOW|READY");
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(BLUE_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(PASS_BUTTON, INPUT_PULLUP);
#if TURNHUB_INPUT_JOYSTICK
  calibrateJoystick();
#else
  pinMode(ACTION_BUTTON, INPUT_PULLUP);
  pinMode(PAUSE_WIN_BUTTON, INPUT_PULLUP);
#endif

#if TURNHUB_STATUS_RING
  TurnHubSigil::statusRingBegin(STATUS_RING_PIN);
#endif
  updateLeds();

  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  stopBuzzer();

  passButton.rawState = passButton.stableState = readButton(PASS_BUTTON);
  actionButton.rawState = actionButton.stableState = readButton(ACTION_BUTTON);
  pauseWinButton.rawState = pauseWinButton.stableState = readButton(PAUSE_WIN_BUTTON);

  Serial.println();
  Serial.println("SIGIL|BOOT|UNASSIGNED|UNIFIED");
  loadSavedPairing();
  sigilDisplay.begin();
  // SPI startup configures its default MISO pin as INPUT; reclaim the pin
  // only after the write-only display has initialized and detached MISO.
  pinMode(PAIR_BUTTON, INPUT_PULLUP);
  pairButton.rawState = digitalRead(PAIR_BUTTON);
  pairButton.stableState = HIGH; // A button held at boot is still a press.
  pairButton.lastDebounceMs = millis();
  Serial.print("SIGIL|PAIR|READY|GPIO|");
  Serial.print(PAIR_BUTTON);
  Serial.println(pairButton.rawState == LOW ? "|DOWN" : "|UP");
  if (atlasKnown) {
    sigilDisplay.showBooting();
  } else {
    sigilDisplay.showUnpaired();
  }

  const BaseType_t displayTaskCreated = xTaskCreatePinnedToCore(
      displayTask,
      "sigil-display",
      DISPLAY_TASK_STACK_BYTES,
      nullptr,
      1,
      &displayTaskHandle,
      1);

  if (displayTaskCreated != pdPASS) {
    displayTaskHandle = nullptr;
    Serial.println("SIGIL|DISPLAY|TASK|ERROR");
  }

  espNowReady = startEspNow();

  if (espNowReady) {
    sendHello();
  }
  Serial.println("SIGIL|READY");
}

void loop() {
  ReceivedPacket received;
  // Bounded so a packet burst cannot starve the buttons.
  for (uint8_t n = 0; receiveQueue && n < RECEIVE_QUEUE_LENGTH &&
       xQueueReceive(receiveQueue, &received, 0) == pdTRUE; ++n) {
    handleEspNowReceive(received.mac,
        received.data, received.length);
  }
#if TURNHUB_INPUT_JOYSTICK
  updateJoystick(millis());
#endif
  updatePassButton();
  updateActionButton(actionButton);
  updateActionButton(pauseWinButton, true);
  updatePairButton();
  updatePairing();
  updateLeds();
  updateBuzzer();
  updateDisplayProfileSync();

  if (displayTaskHandle == nullptr) {
    // Safe fallback if the display worker could not be created.
    updateDisplay();
  }

  if (espNowReady && millis() - lastHelloMs >= HELLO_INTERVAL_MS) {
    sendHello();
  }

  delay(1);
}
