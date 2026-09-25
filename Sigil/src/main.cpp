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
#include <nvs_flash.h>
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
#ifndef TURNHUB_DISPLAY_OLED
#define TURNHUB_DISPLAY_OLED 0
#endif
#ifndef TURNHUB_INPUT_DPAD
#define TURNHUB_INPUT_DPAD 0
#endif
#if TURNHUB_INPUT_JOYSTICK && TURNHUB_INPUT_DPAD
#error "Choose one five-key input: TURNHUB_INPUT_JOYSTICK or TURNHUB_INPUT_DPAD"
#endif
// Five-key Sigils show Atlas's action menu (CAPABILITY_MENU).
#define TURNHUB_MENU (TURNHUB_INPUT_JOYSTICK || TURNHUB_INPUT_DPAD)
#if TURNHUB_MENU
#include "sigil_menu.h"
#endif
#ifndef TURNHUB_STATUS_RING
#define TURNHUB_STATUS_RING 0
#endif
#if TURNHUB_STATUS_RING
#if !TURNHUB_MENU
#error "The status ring uses GPIO26, which is the Pass button in three-button builds"
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

#if TURNHUB_STATUS_RING
// Status light: NeoPixel Jewel 7 Data Input, via 330 ohm (J10). Powered from
// 5V (J1). Both hardware Sigils use it; there is no separate LED.
constexpr uint8_t STATUS_RING_PIN = 26;
#else
// Status light: one RGB LED (or three single LEDs), PWM on every channel.
// The Wokwi diagram's three LEDs.
constexpr uint8_t BLUE_LED = 27;
constexpr uint8_t GREEN_LED = 14;
constexpr uint8_t RED_LED = 13;
#endif
#if TURNHUB_MENU
// Five keys (Up, Down, Left, Right, Select). Each is a debounced button: a
// real GPIO switched to GND, or a virtual pin (0xE0+) read from the stick.
// Until Atlas sends a menu, the keys fall back to the three-button gestures
// through virtual pins: Select = PASS, Right = Action, Down = Pause/Win.
constexpr uint8_t STICK_UP_PIN = 0xE0;  // 0xE0-0xE3: Up, Down, Left, Right.
constexpr uint8_t ACTION_BUTTON = 0xF0;
constexpr uint8_t PAUSE_WIN_BUTTON = 0xF1;
constexpr uint8_t PASS_BUTTON = 0xF2;
#if TURNHUB_INPUT_JOYSTICK
// Analog thumbstick (powered from 3.3 V, never 5 V: VRX/VRY swing to the
// supply); clicking it is Select. VRX/VRY must be ADC1 pins: ADC2 is unusable
// while ESP-NOW has the radio. GPIO25 is unused; GPIO26 drives the ring.
constexpr uint8_t JOYSTICK_X_PIN = 34;     // VRX, input-only ADC1 (J15).
constexpr uint8_t JOYSTICK_Y_PIN = 35;     // VRY, input-only ADC1 (J14).
constexpr uint8_t JOYSTICK_CALIBRATION_SAMPLES = 16;
constexpr uint32_t JOYSTICK_SAMPLE_MS = 5;
constexpr uint8_t KEY_PINS[] = {STICK_UP_PIN, STICK_UP_PIN + 1, STICK_UP_PIN + 2,
    STICK_UP_PIN + 3, 32 /* SW (J13) */};
#else
// Five-button d-pad, each a switch to GND with the internal pull-up.
constexpr uint8_t KEY_PINS[] = {25 /* Up, J11 */, 27 /* Down, J9 */, 19 /* Left, A12 */,
    21 /* Right, A14 */, 32 /* Select, J13 */};
#endif
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
#if TURNHUB_MENU
    | TurnHubProtocol::CAPABILITY_MENU
#endif
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
#if TURNHUB_MENU
ButtonState keys[] = {ButtonState(KEY_PINS[0]), ButtonState(KEY_PINS[1]),
    ButtonState(KEY_PINS[2]), ButtonState(KEY_PINS[3]), ButtonState(KEY_PINS[4])};
static_assert(sizeof(keys) / sizeof(keys[0]) == TurnHubSigil::KEY_COUNT, "One button per key");
// The e-ink panel redraws in seconds, so it gets the fixed-key compass; the
// OLED redraws instantly and gets the scrolling list.
TurnHubSigil::SigilMenu sigilMenu(
    TURNHUB_DISPLAY_OLED ? TurnHubSigil::MenuLayout::List : TurnHubSigil::MenuLayout::Compass);
TurnHubProtocol::SigilAction lastSelectedAction = TurnHubProtocol::SigilAction::Count;
// Menu snapshot for the display task (guarded by displayProfileMux).
TurnHubSigil::MenuView publishedMenuView;
TurnHubSigil::MenuView pendingMenuView;
bool menuViewChanged = false;
#endif
// The e-ink menu Sigil also draws Atlas's profile picker (ProfilePickerPacket).
#define TURNHUB_PICKER (TURNHUB_MENU && !TURNHUB_DISPLAY_OLED)
#if TURNHUB_PICKER
// Latest page from Atlas (guarded by displayProfileMux). While open, the keys
// go to the picker (PickerKey) instead of the menu.
TurnHubProtocol::ProfilePickerPacket pendingPicker{};
volatile bool pickerActive = false;
bool pickerChanged = false;
#endif
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
#if !TURNHUB_STATUS_RING
TurnHubSigil::Rgb shownSingle;
#endif
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

// Renders the current light state to the Jewel ring (hardware Sigils) or the
// RGB LED pins (Wokwi; PWM on all three for full color). Hardware changes
// only when the frame differs.
void updateLeds() {
  const uint32_t nowMs = millis();
  if (ledOutputValid && nowMs - lastLedFrameMs < LED_FRAME_MS) return;
  lastLedFrameMs = nowMs;
  const TurnHubSigil::LedFrame frame = ledModel.render(nowMs);
#if TURNHUB_STATUS_RING
  TurnHubSigil::statusRingShow(frame);
#else
  if (!ledOutputValid || frame.single != shownSingle) {
    analogWrite(RED_LED, frame.single.r);
    analogWrite(GREEN_LED, frame.single.g);
    analogWrite(BLUE_LED, frame.single.b);
    shownSingle = frame.single;
  }
#endif
  ledOutputValid = true;
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
  if (pin >= STICK_UP_PIN && pin <= STICK_UP_PIN + 3) {
    static constexpr TurnHubSigil::StickDirection DIRECTIONS[] = {
        TurnHubSigil::StickDirection::Up, TurnHubSigil::StickDirection::Down,
        TurnHubSigil::StickDirection::Left, TurnHubSigil::StickDirection::Right};
    return stick.direction() == DIRECTIONS[pin - STICK_UP_PIN] ? LOW : HIGH;
  }
#endif
#if TURNHUB_MENU
  // Legacy gestures while Atlas sends no menu (an older Atlas).
  const auto legacy = [](TurnHubSigil::Key key) {
    return !sigilMenu.active() && keys[static_cast<uint8_t>(key)].stableState == LOW ? LOW : HIGH;
  };
  if (pin == PASS_BUTTON) return legacy(TurnHubSigil::Key::Select);
  if (pin == ACTION_BUTTON) return legacy(TurnHubSigil::Key::Right);
  if (pin == PAUSE_WIN_BUTTON) return legacy(TurnHubSigil::Key::Down);
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
  bool menuChanged = false;
#if TURNHUB_PICKER
  // The picker replaces every other screen while Atlas keeps it open.
  static bool pickerShown = false;
  TurnHubProtocol::ProfilePickerPacket picker{};
  portENTER_CRITICAL(&displayProfileMux);
  const bool showPicker = pickerActive && sigilId != UNASSIGNED_SIGIL_ID;
  const bool newPage = pickerChanged;
  picker = pendingPicker;
  pickerChanged = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (showPicker) {
    displayNeedsRefresh = false;
    if (newPage || !pickerShown) sigilDisplay.showPicker(picker);
    pickerShown = true;
    return;
  }
  if (pickerShown) {
    // Closed: redraw whatever Atlas says is current.
    pickerShown = false;
    displayNeedsRefresh = true;
  }
#endif
#if TURNHUB_MENU
  TurnHubSigil::MenuView menuView;
  portENTER_CRITICAL(&displayProfileMux);
  if (menuViewChanged) {
    menuView = pendingMenuView;
    menuViewChanged = false;
    menuChanged = true;
  }
  portEXIT_CRITICAL(&displayProfileMux);
  if (menuChanged) sigilDisplay.setMenuView(menuView);
#endif
  TurnHubProtocol::GameDisplayPacket currentGame{};
  portENTER_CRITICAL(&displayProfileMux);
  const bool hasGame = gameDisplayValid;
  if (hasGame) currentGame = pendingGameDisplay;
  portEXIT_CRITICAL(&displayProfileMux);
  if (hasGame && currentGame.sigilId == sigilId) {
    applyPendingSeatNames();
    // Clear before drawing; packets arriving while the panel is busy wake us again.
    displayNeedsRefresh = false;
    if (menuChanged || !renderedGameValid || memcmp(&renderedGame, &currentGame, sizeof(currentGame))) {
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
#if !TURNHUB_DISPLAY_OLED
    // State, game and menu packets arrive together: one e-ink refresh for all.
    vTaskDelay(pdMS_TO_TICKS(80));
#endif
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
#if TURNHUB_MENU
  sigilMenu.clear();
#endif
#if TURNHUB_PICKER
  portENTER_CRITICAL(&displayProfileMux);
  pickerActive = false;
  pickerChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
#endif
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
#if TURNHUB_PICKER
  if (length == sizeof(TurnHubProtocol::ProfilePickerPacket)) {
    TurnHubProtocol::ProfilePickerPacket page{};
    memcpy(&page, incomingData, sizeof(page));
    if (!atlasKnown || memcmp(mac, atlasMac, 6) || page.sigilId != sigilId ||
        !TurnHubProtocol::validProfilePicker(page)) return;
    const bool open = page.mode != TurnHubProtocol::PickerMode::Closed;
    portENTER_CRITICAL(&displayProfileMux);
    const bool changed = open != pickerActive ||
        (open && memcmp(&page, &pendingPicker, sizeof(page)) != 0);
    pendingPicker = page;
    pickerActive = open;
    pickerChanged = pickerChanged || changed;
    portEXIT_CRITICAL(&displayProfileMux);
    if (changed) { displayNeedsRefresh = true; notifyDisplayTask(); }
    return;
  }
#endif
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
#if TURNHUB_MENU
      if (packet.value == static_cast<int32_t>(PacketType::SelectAction) &&
          lastSelectedAction == TurnHubProtocol::SigilAction::Pass) {
        ledModel.flashPassAck(millis());
      }
#endif
      break;

#if TURNHUB_MENU
    case PacketType::MenuState2:
      sigilMenu.applyMenuState2(packet.value, millis());
      break;
    case PacketType::MenuState:
      sigilMenu.applyMenuState(packet.value, millis());
      break;
#endif

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

    case PacketType::FactoryReset:
      // An Admin chose Factory reset for this Sigil in Device Settings.
      // Erase everything saved (pairing included) and start over as new.
      if (packet.value != TurnHubProtocol::FACTORY_RESET_CONFIRM) break;
      Serial.println("SIGIL|FACTORY_RESET|ERASING");
      Serial.flush();
      nvs_flash_erase();
      delay(100);
      ESP.restart();
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

#if TURNHUB_MENU
// Hands the display task a new menu snapshot when what it shows changed.
void publishMenuView() {
  const TurnHubSigil::MenuView view = sigilMenu.view();
  if (view == publishedMenuView) return;
  publishedMenuView = view;
  portENTER_CRITICAL(&displayProfileMux);
  pendingMenuView = view;
  menuViewChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

// Five-key input: key edges go to the menu, and a finished choice becomes a
// SelectAction. Hold progress drives the status light.
void updateMenuKeys() {
  const uint32_t nowMs = millis();
  sigilMenu.setHoldTimes(static_cast<uint16_t>(longPressMs), static_cast<uint16_t>(winHoldMs));
  for (uint8_t k = 0; k < TurnHubSigil::KEY_COUNT; ++k) {
    if (!debouncedEdge(keys[k], nowMs)) continue;
    const auto key = static_cast<TurnHubSigil::Key>(k);
#if TURNHUB_PICKER
    if (pickerActive) {
      // Keys choose on the picker's page, never a menu action.
      if (keys[k].stableState == LOW) {
        portENTER_CRITICAL(&displayProfileMux);
        const uint8_t revision = pendingPicker.revision;
        portEXIT_CRITICAL(&displayProfileMux);
        Serial.print("SIGIL|");
        Serial.print(sigilId);
        Serial.print("|PICKER|KEY|");
        Serial.println(k);
        sendPacket(PacketType::PickerKey, TurnHubProtocol::encodePickerKey(
            static_cast<TurnHubProtocol::PickerKeyCode>(k), revision));
      } else {
        sigilMenu.keyUp(key, nowMs);
      }
      continue;
    }
#endif
    if (keys[k].stableState == LOW) {
      sigilMenu.keyDown(key, nowMs);
    } else {
      sigilMenu.keyUp(key, nowMs);
    }
  }
  const TurnHubSigil::MenuChoice choice = sigilMenu.update(nowMs);
  if (choice.ready) {
    lastSelectedAction = choice.action;
    Serial.print("SIGIL|");
    Serial.print(sigilId);
    Serial.print("|MENU|");
    Serial.println(TurnHubSigil::sigilActionLabel(choice.action));
    sendPacket(PacketType::SelectAction, TurnHubProtocol::encodeSelectAction(choice.action, choice.revision));
  }
  ledModel.setHoldProgress(sigilMenu.holdProgress(nowMs));
  publishMenuView();
}
#endif

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

#if !TURNHUB_STATUS_RING
  pinMode(BLUE_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
#endif
#if TURNHUB_MENU
  for (auto &key : keys) {
    if (key.pin < STICK_UP_PIN) pinMode(key.pin, INPUT_PULLUP);
  }
#if TURNHUB_INPUT_JOYSTICK
  calibrateJoystick();
#endif
  for (auto &key : keys) key.rawState = key.stableState = readButton(key.pin);
#else
  pinMode(PASS_BUTTON, INPUT_PULLUP);
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
#if TURNHUB_MENU
  updateMenuKeys();
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
