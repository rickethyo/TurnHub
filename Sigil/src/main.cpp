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

#include "atlas_link.h"
#include "firmware_version.h"
#include "protocol.h"
#include "sigil_display.h"
#include "sigil_led.h"
#include "received_packet.h"
#include "pairing_v2.h"
#include "secure_session.h"
#include "secure_link_mbedtls.h"
#ifndef TURNHUB_WOKWI
#include "firmware_package.h"

// This build's identity, read by tools/firmware/thfw.py when it packages
// firmware.bin for OTA (SIGIL_OTA.md). The Wokwi build has none, so it can't
// be packaged.
#ifdef TURNHUB_DISPLAY_OLED
constexpr TurnHubFirmwarePackage::Product SIGIL_PRODUCT = TurnHubFirmwarePackage::Product::SigilOled;
#else
constexpr TurnHubFirmwarePackage::Product SIGIL_PRODUCT = TurnHubFirmwarePackage::Product::SigilEink;
#endif
TURNHUB_FIRMWARE_DESCRIPTOR(sigilFirmwareDescriptor, SIGIL_PRODUCT, TurnHubSigilFirmware::MAJOR,
    TurnHubSigilFirmware::MINOR, TurnHubSigilFirmware::PATCH, TurnHubProtocol::VERSION);
#include "sigil_updater.h"
#define TURNHUB_OTA 1
#else
#define TURNHUB_OTA 0
#endif

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
#include "picker_list.h"
#include "life_adjust.h"
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

// Hardware-type strap (carrier header A7, GPIO4): left open on an E-ink
// Sigil, wired to GND on an OLED Sigil. It stays with the board through every
// flash and erase, so a build for the other display refuses to start.
constexpr uint8_t HW_TYPE_STRAP_PIN = 4;

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
// Menu Sigils also draw Atlas's profile picker (ProfilePickerPacket): the
// e-ink as a compass, the OLED as a list (picker_list.h).
#define TURNHUB_PICKER TURNHUB_MENU
#if TURNHUB_PICKER
// Latest page from Atlas (guarded by displayProfileMux). While open, the keys
// go to the picker (PickerKey) instead of the menu. pickerCursor is the OLED
// list row, reset on every new page.
TurnHubProtocol::ProfilePickerPacket pendingPicker{};
volatile bool pickerActive = false;
bool pickerChanged = false;
uint8_t pickerCursor = 0;
#endif
#if TURNHUB_MENU
// Life (AdjustLife, LifeRequest): Left/Right presses batched by lifeAdjuster;
// the request shown for this Sigil's answer. lifeKeyRouted remembers which
// key-downs went to life so their releases do not reach the menu.
TurnHubSigil::LifeAdjuster lifeAdjuster;
TurnHubProtocol::LifeRequestFields lifeRequest;
uint8_t seatAvatars[2] = {0, 0};  // From SeatColor; drawn by the OLED.
int32_t startingLife = 0;  // From StartingLife: sizes the life heart.
uint8_t passingPlayer = 0;  // From PassPending: whose pass is pending.
bool lifeKeyRouted[TurnHubSigil::KEY_COUNT] = {};
// Snapshot for the display task (guarded by displayProfileMux).
TurnHubSigil::LifeOverlay publishedLifeOverlay;
TurnHubSigil::LifeOverlay pendingLifeOverlay;
bool lifeOverlayChanged = false;
#endif
SigilDisplay &sigilDisplay = TurnHubSigil::getSigilDisplay();

bool espNowReady = false;
bool atlasKnown = false;
// Whether the paired Atlas still answers; atlasLostShown mirrors it for the
// display task.
TurnHubSigil::AtlasLink atlasLink;
volatile bool atlasLostShown = false;
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
// Pairing v2 (pairing_v2.h, SECURE_LINK.md): the key agreement and code check,
// and the pair key for the saved Atlas (none for a Sigil paired the old way).
// Traffic stays cleartext until the secure-session step.
constexpr char PAIRING_KEY_V2[] = "atlas_k";
TurnHubSecureLink::MbedtlsCrypto linkCrypto;
TurnHubSecureLink::SigilPairing pairingV2;
TurnHubSecureLink::PairRequest2Packet pairRequest2{};
uint8_t atlasPairKey[TurnHubSecureLink::KEY_BYTES] = {};
bool atlasPairKeyValid = false;
// The secure session with the saved Atlas (secure_session.h). Everything
// but pairing and the handshake travels sealed in it; RAM only.
TurnHubSecureLink::SigilSession linkSession;
// When a packet from Atlas last opened (or its handshake answer arrived).
uint32_t lastAtlasFrameMs = 0;
// The code on screen while the owner checks it (read by the display task).
volatile bool pairingCodeShown = false;
volatile uint16_t pairingCodeValue = 0;
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

#if TURNHUB_OTA
// Sigil OTA (sigil_updater.h). While an update runs, loop() is inside
// updater.run(); the display task draws the progress below.
TurnHubSigil::SigilUpdater updater;
bool updateScreenActive = false;   // Guarded by displayProfileMux.
bool updateScreenChanged = false;
char updateScreenText[24] = {};
int8_t updateScreenPercent = -1;
#endif
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

// Every packet to Atlas travels sealed in the current session; with none
// (before Atlas answers SecureHello) nothing is sent.
void sendPacket(PacketType type, int32_t value = 0) {
  if (!espNowReady || !atlasKnown || !linkSession.ready() || !ensurePeer(atlasMac)) {
    return;
  }
  const Packet packet = TurnHubProtocol::makePacket(type, sigilId, value);
  uint8_t frame[sizeof(Packet) + TurnHubSecureLink::SECURE_OVERHEAD];
  const size_t length = linkSession.seal(linkCrypto, &packet, sizeof(packet), frame, sizeof(frame));
  if (length == 0) return;
  const esp_err_t result = esp_now_send(atlasMac, frame, length);

  if (result != ESP_OK) {
    Serial.print("SIGIL|ESP_NOW|SEND_ERROR|");
    Serial.println(static_cast<int>(result));
  }
}

// A pairing v2 request (38 bytes), broadcast like the old PairRequest.
void sendBroadcastRaw(const void *data, size_t length) {
  if (!espNowReady || !ensurePeer(BROADCAST_MAC)) return;
  const esp_err_t result =
      esp_now_send(BROADCAST_MAC, static_cast<const uint8_t *>(data), length);
  if (result != ESP_OK) {
    Serial.print("SIGIL|ESP_NOW|SEND_ERROR|");
    Serial.println(static_cast<int>(result));
  }
}

// Every HELLO_INTERVAL_MS. With a session and Atlas heard lately, a sealed
// Hello keeps it alive; otherwise (boot, pairing, Atlas restarted or quiet)
// a SecureHello starts a new one.
void sendHello() {
  lastHelloMs = millis();
  if (!atlasKnown || !linkSession.hasKey()) return;
  const int32_t helloInfo = TurnHubProtocol::encodeHelloInfo(
      TurnHubSigilFirmware::MAJOR,
      TurnHubSigilFirmware::MINOR,
      TurnHubSigilFirmware::PATCH,
      DEVICE_CAPABILITIES);
  if (linkSession.ready() && millis() - lastAtlasFrameMs <= 2 * HELLO_INTERVAL_MS) {
    sendPacket(PacketType::Hello, helloInfo);
    return;
  }
  TurnHubSecureLink::SecureHelloPacket hello;
  if (!espNowReady || !linkSession.makeHello(linkCrypto, helloInfo, hello) ||
      !ensurePeer(atlasMac)) {
    return;
  }
  esp_now_send(atlasMac, reinterpret_cast<const uint8_t *>(&hello), sizeof(hello));
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
#if TURNHUB_PICKER
  static bool pickerShown = false;
#endif
  // Atlas lost replaces every screen. Once it answers again, redraw the last
  // state it sent (Atlas resends it too, but unchanged packets draw nothing).
  static bool lostDrawn = false;
  static bool codeDrawn = false;
  static uint16_t drawnCode = 0;
  bool updateEnded = false;
#if TURNHUB_OTA
  // A firmware update outranks every screen.
  static bool updateDrawn = false;
  char updateText[sizeof(updateScreenText)];
  portENTER_CRITICAL(&displayProfileMux);
  const bool updating = updateScreenActive;
  const bool updateChanged = updateScreenChanged;
  memcpy(updateText, updateScreenText, sizeof(updateText));
  const int8_t updatePercent = updateScreenPercent;
  updateScreenChanged = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (updating) {
    displayNeedsRefresh = false;
    if (updateChanged || !updateDrawn) sigilDisplay.showUpdate(updateText, updatePercent);
    updateDrawn = true;
    return;
  }
  updateEnded = updateDrawn;
  updateDrawn = false;
#endif
  if (pairingCodeShown) {
    displayNeedsRefresh = false;
    const uint16_t code = pairingCodeValue;
    if (!codeDrawn || code != drawnCode) sigilDisplay.showPairingCode(code);
    codeDrawn = true;
    drawnCode = code;
    return;
  }
  // After the code or Atlas lost, redraw whatever comes next in full.
  bool overlayEnded = codeDrawn || updateEnded;
  codeDrawn = false;
  if (atlasLostShown && sigilId != UNASSIGNED_SIGIL_ID) {
    displayNeedsRefresh = false;
    if (!lostDrawn || overlayEnded) sigilDisplay.showAtlasLost(sigilId);
    lostDrawn = true;
    return;
  }
  overlayEnded = overlayEnded || lostDrawn;
  if (overlayEnded) {
    lostDrawn = false;
    renderedGameValid = false;
#if TURNHUB_PICKER
    pickerShown = false;
#endif
    displayNeedsRefresh = true;
  }
  bool menuChanged = false;
#if TURNHUB_PICKER
  // The picker replaces every other screen while Atlas keeps it open.
  TurnHubProtocol::ProfilePickerPacket picker{};
  portENTER_CRITICAL(&displayProfileMux);
  const bool showPicker = pickerActive && sigilId != UNASSIGNED_SIGIL_ID;
  const bool newPage = pickerChanged;
  picker = pendingPicker;
  const uint8_t cursor = pickerCursor;
  pickerChanged = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (showPicker) {
    displayNeedsRefresh = false;
    if (newPage || !pickerShown) sigilDisplay.showPicker(picker, cursor);
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
  portENTER_CRITICAL(&displayProfileMux);
  const bool lifeChanged = lifeOverlayChanged;
  const TurnHubSigil::LifeOverlay lifeOverlay = pendingLifeOverlay;
  lifeOverlayChanged = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (lifeChanged) {
    sigilDisplay.setLifeOverlay(lifeOverlay);
    menuChanged = true;
  }
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
    // Sleep until a packet wakes us or the panel's clean-up is due.
    const uint32_t due = sigilDisplay.idleWorkDueInMs(millis());
    if (ulTaskNotifyTake(pdTRUE, due == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(due)) == 0) {
      sigilDisplay.idleWork(millis());
      continue;
    }
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
    erased = (!prefs.isKey(PAIRING_KEY) || prefs.remove(PAIRING_KEY)) &&
        (!prefs.isKey(PAIRING_KEY_V2) || prefs.remove(PAIRING_KEY_V2));
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
  atlasLink.stop();
  atlasLostShown = false;
  pairingV2.cancel();
  pairingCodeShown = false;
  TurnHubSecureLink::wipe(atlasPairKey, sizeof(atlasPairKey));
  atlasPairKeyValid = false;
  linkSession.clear();
  ledModel.setAtlasLost(false, millis());
  ledModel.clear();
#if TURNHUB_MENU
  sigilMenu.clear();
  lifeAdjuster.cancel();
  lifeRequest = TurnHubProtocol::LifeRequestFields{};
  seatAvatars[0] = seatAvatars[1] = 0;
  startingLife = 0;
  passingPlayer = 0;
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

// Secure-link crypto check against published vectors (SECURE_LINK.md). Not
// used by the radio yet; logged so each board's result is on record.
bool runSecureLinkSelfTest() {
  TurnHubSecureLink::MbedtlsCrypto crypto;
  const uint32_t startMs = millis();
  const TurnHubSecureLink::SelfTestStep step = TurnHubSecureLink::knownAnswerTest(crypto);
  if (step != TurnHubSecureLink::SelfTestStep::Pass) {
    Serial.printf("SIGIL|SECURE_LINK|SELF_TEST|FAIL|%u\n", static_cast<unsigned>(step));
    return false;
  }
  Serial.printf("SIGIL|SECURE_LINK|SELF_TEST|PASS|%lums\n",
      static_cast<unsigned long>(millis() - startMs));
  return true;
}

// Ends a pairing v2 code check without storing anything (rejected on Atlas,
// lapsed, or the Sigil gave up). Any earlier pairing is untouched.
void endPairingCodeCheck(const char *reason) {
  pairingV2.cancel();
  pairingActive = false;
  pairingCodeShown = false;
  ledModel.setPairing(false, millis());
  Serial.print("SIGIL|PAIR|V2|");
  Serial.println(reason);
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

// Atlas confirmed the code: save Atlas, the slot and the pair key, then carry
// on exactly as after the old pairing.
void storePairingV2(const uint8_t *mac, uint8_t slot, const uint8_t *key) {
  uint8_t binding[PAIRING_BINDING_SIZE];
  memcpy(binding, mac, 6);
  binding[6] = slot;
  Preferences prefs;
  bool stored = false;
  if (prefs.begin(PAIRING_NAMESPACE, false)) {
    stored = prefs.putBytes(PAIRING_KEY, binding, sizeof(binding)) == sizeof(binding) &&
        prefs.putBytes(PAIRING_KEY_V2, key, TurnHubSecureLink::KEY_BYTES) ==
            TurnHubSecureLink::KEY_BYTES;
    prefs.end();
  }
  if (!stored) {
    endPairingCodeCheck("STORE_ERROR");
    return;
  }
  memcpy(atlasPairKey, key, TurnHubSecureLink::KEY_BYTES);
  atlasPairKeyValid = true;
  linkSession.configure(slot, key);
  pairingCodeShown = false;
  rememberAtlas(mac);
  sigilId = slot;
  atlasLink.start(millis());
  ledModel.setPairing(false, millis());
  profileSyncStartPending = true;
  queueReadyDisplay();
  Serial.println("SIGIL|PAIR|SUCCESS|SECURE");
  sendHello();
}

// Shows or clears "Atlas lost" (atlas_link.h). Runs on the loop task.
void applyAtlasLinkChange(TurnHubSigil::LinkChange change) {
  if (change == TurnHubSigil::LinkChange::None) return;
  const bool lost = change == TurnHubSigil::LinkChange::Lost;
  if (lost) {
#if TURNHUB_MENU
    // Stale: nothing here can reach Atlas. Atlas resends the menu and any
    // life request with its next Hello answer; an unsent life change is
    // dropped rather than applied late.
    sigilMenu.clear();
    lifeAdjuster.cancel();
    lifeRequest = TurnHubProtocol::LifeRequestFields{};
#endif
  }
  ledModel.setAtlasLost(lost, millis());
  atlasLostShown = lost;
  Serial.println(lost ? "SIGIL|ATLAS|LOST" : "SIGIL|ATLAS|RESTORED");
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

void noteAtlasHeard() {
  lastAtlasFrameMs = millis();
  applyAtlasLinkChange(atlasLink.heard(millis()));
}

// A packet from Atlas that opened in the current session (handleEspNowReceive).
#if TURNHUB_OTA
void sendUpdateStatus(int32_t value) {
  sendPacket(PacketType::SigilUpdateStatus, value);
}

// Called from inside updater.run(), which blocks loop(): the light is driven
// from here, the screen by the display task.
void showUpdateProgress(const char *status, int8_t percent) {
  static char shownText[sizeof(updateScreenText)] = {};
  static int8_t shownBucket = -2;
#if TURNHUB_DISPLAY_OLED
  const int8_t bucket = percent;
#else
  // E-ink: a full refresh per quarter, not per 10%.
  const int8_t bucket = percent < 0 ? -1 : static_cast<int8_t>(percent / 25);
#endif
  const bool redraw = strcmp(shownText, status) != 0 || bucket != shownBucket;
  if (redraw) {
    strncpy(shownText, status, sizeof(shownText) - 1);
    shownBucket = bucket;
    portENTER_CRITICAL(&displayProfileMux);
    strncpy(updateScreenText, status, sizeof(updateScreenText) - 1);
    updateScreenPercent = percent;
    updateScreenActive = true;
    updateScreenChanged = true;
    portEXIT_CRITICAL(&displayProfileMux);
    notifyDisplayTask();
  }
  ledModel.setUpdating(true, percent < 0 ? 0 : static_cast<uint8_t>(percent), millis());
  ledOutputValid = false;
  updateLeds();
}

void endUpdateScreen() {
  portENTER_CRITICAL(&displayProfileMux);
  updateScreenActive = false;
  updateScreenChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
  ledModel.setUpdating(false, 0, millis());
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

void handleUpdateOffer(const uint8_t *mac, const uint8_t *incomingData) {
  TurnHubProtocol::SigilUpdateOfferPacket offer{};
  memcpy(&offer, incomingData, sizeof(offer));
  if (!atlasKnown || memcmp(mac, atlasMac, 6) != 0) return;
  noteAtlasHeard();
  updater.offer(offer, sigilId);
  TurnHubSecureLink::wipe(&offer, sizeof(offer));
}
#endif

void handleAtlasPacket(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
#if TURNHUB_OTA
  // Checked before the version test below: a newer Atlas may still update
  // this Sigil (MIN_UPDATABLE_VERSION).
  if (length == sizeof(TurnHubProtocol::SigilUpdateOfferPacket)) {
    handleUpdateOffer(mac, incomingData);
    return;
  }
#endif
#if TURNHUB_PICKER
  if (length == sizeof(TurnHubProtocol::ProfilePickerPacket)) {
    TurnHubProtocol::ProfilePickerPacket page{};
    memcpy(&page, incomingData, sizeof(page));
    if (!atlasKnown || memcmp(mac, atlasMac, 6) || page.sigilId != sigilId ||
        !TurnHubProtocol::validProfilePicker(page)) return;
    noteAtlasHeard();
    const bool open = page.mode != TurnHubProtocol::PickerMode::Closed;
    portENTER_CRITICAL(&displayProfileMux);
    const bool changed = open != pickerActive ||
        (open && memcmp(&page, &pendingPicker, sizeof(page)) != 0);
    if (page.revision != pendingPicker.revision || !open) pickerCursor = 0;
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
    noteAtlasHeard();
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

  if (!atlasKnown || memcmp(mac, atlasMac, 6) != 0) return;

  if (packet.type == PacketType::Ack &&
      packet.value == static_cast<int32_t>(PacketType::Hello)) {
    if (packet.sigilId >= TurnHubProtocol::MAX_SIGILS) return;
    noteAtlasHeard();

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
  noteAtlasHeard();

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
    case PacketType::LifeRequest:
      lifeRequest = TurnHubProtocol::decodeLifeRequest(packet.value);
      break;
    case PacketType::SeatColor: {
      ledModel.applySeatColor(packet.value);
      const uint8_t slot = TurnHubProtocol::seatColorSlot(packet.value);
      if (slot == 1 || slot == 2) seatAvatars[slot - 1] = TurnHubProtocol::seatAvatar(packet.value);
      break;
    }
    case PacketType::MenuState:
      sigilMenu.applyMenuState(packet.value, millis());
      break;
    case PacketType::StartingLife:
      startingLife = constrain(packet.value, 0, 1000000);
      break;
    case PacketType::PassPending:
      passingPlayer = static_cast<uint8_t>(constrain(packet.value, 0, 16));
      break;
#endif

    case PacketType::LedState:
      ledModel.applyLedState(packet.value, millis());
      break;

    case PacketType::TableClock:
      ledModel.syncTableClock(packet.value, millis());
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

// Everything the radio delivers. Pairing and the session handshake are the
// only unsealed packets; anything else from Atlas must open in the current
// session (right key, rising counter) or it is dropped.
void handleEspNowReceive(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
  PacketType type;
  if (!TurnHubSecureLink::framePacketType(incomingData, static_cast<size_t>(length), type)) return;
  if (length == sizeof(TurnHubSecureLink::PairAccept2Packet)) {
    TurnHubSecureLink::PairAccept2Packet accept{};
    memcpy(&accept, incomingData, sizeof(accept));
    uint8_t ownMac[6];
    if (!pairingActive || esp_wifi_get_mac(WIFI_IF_STA, ownMac) != ESP_OK ||
        !pairingV2.accept(linkCrypto, accept, mac, ownMac, millis())) {
      return;
    }
    // The window has done its job; the code check keeps the pairing light on.
    pairingActive = false;
    pairingCodeValue = pairingV2.code();
    pairingCodeShown = true;
    Serial.printf("SIGIL|PAIR|V2|CODE_SHOWN|%u\n", static_cast<unsigned>(accept.sigilId));
    displayNeedsRefresh = true;
    notifyDisplayTask();
    return;
  }
  if (length == sizeof(TurnHubSecureLink::PairResultPacket)) {
    TurnHubSecureLink::PairResultPacket result{};
    memcpy(&result, incomingData, sizeof(result));
    uint8_t key[TurnHubSecureLink::KEY_BYTES];
    const TurnHubSecureLink::PairVerdict verdict = pairingV2.result(linkCrypto, result, mac, key);
    if (verdict == TurnHubSecureLink::PairVerdict::Confirmed) {
      storePairingV2(mac, result.sigilId, key);
    } else if (verdict == TurnHubSecureLink::PairVerdict::Rejected) {
      endPairingCodeCheck("REJECTED");
    }
    TurnHubSecureLink::wipe(key, sizeof(key));
    return;
  }
  if (!atlasKnown || memcmp(mac, atlasMac, 6) != 0) return;
  if (length == sizeof(TurnHubSecureLink::SecureHelloAckPacket) &&
      type == PacketType::SecureHelloAck) {
    TurnHubSecureLink::SecureHelloAckPacket ack{};
    memcpy(&ack, incomingData, sizeof(ack));
    if (linkSession.acceptAck(linkCrypto, ack)) {
      Serial.println("SIGIL|SECURE|SESSION_READY");
      noteAtlasHeard();
    }
    return;
  }
  if (type != PacketType::Secure) return;  // Unsealed from Atlas: dropped.
  uint8_t inner[TurnHubSigil::ReceivedPacket::MAX_BYTES];
  const size_t innerLength = linkSession.open(linkCrypto, incomingData,
      static_cast<size_t>(length), inner, sizeof(inner));
  if (innerLength == 0) return;
  handleAtlasPacket(mac, inner, static_cast<int>(innerLength));
  TurnHubSecureLink::wipe(inner, innerLength);  // An update offer holds the Wi-Fi password.
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

// The player this Sigil shows first (the one Left/Right change).
uint8_t shownPlayer() {
  portENTER_CRITICAL(&displayProfileMux);
  int32_t state = displayPayload;
  if (gameDisplayValid) state = pendingGameDisplay.state;
  portEXIT_CRITICAL(&displayProfileMux);
  return TurnHubProtocol::displayPrimaryPlayer(state);
}

// Hands the display task the life overlay when what it shows changed. The
// e-ink leaves the running total to the status ring (a redraw takes seconds).
void publishLifeOverlay() {
  TurnHubSigil::LifeOverlay overlay;
  overlay.request = lifeRequest;
  overlay.avatar[0] = seatAvatars[0];
  overlay.avatar[1] = seatAvatars[1];
  overlay.startingLife = startingLife;
  // Atlas offers Undo pass only while this Sigil's pass waits out its grace.
  overlay.passPending = sigilMenu.active() && (sigilMenu.actions() &
      TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::CancelPass)) != 0;
  overlay.passingPlayer = passingPlayer;
  ledModel.setPassPending(overlay.passPending || passingPlayer != 0, overlay.passPending, millis());
#if TURNHUB_DISPLAY_OLED
  overlay.pending = lifeAdjuster.pending();
  overlay.pendingPlayer = lifeAdjuster.player();
#endif
  if (overlay == publishedLifeOverlay) return;
  publishedLifeOverlay = overlay;
  portENTER_CRITICAL(&displayProfileMux);
  pendingLifeOverlay = overlay;
  lifeOverlayChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

// Left/Right: answer a shown life request (Right approves, Left denies), or
// change life when no menu action has the key (and the OLED list is closed).
// True when the key was used here.
bool routeLifeKey(TurnHubSigil::Key key, bool down, uint32_t nowMs) {
  const uint8_t k = static_cast<uint8_t>(key);
  if (!down) {
    if (!lifeKeyRouted[k]) return false;
    lifeKeyRouted[k] = false;
    lifeAdjuster.release(nowMs);
    return true;
  }
  if (key != TurnHubSigil::Key::Left && key != TurnHubSigil::Key::Right) return false;
  const bool right = key == TurnHubSigil::Key::Right;
  if (lifeRequest.target != 0) {
    Serial.print("SIGIL|");
    Serial.print(sigilId);
    Serial.println(right ? "|LIFE|APPROVE" : "|LIFE|DENY");
    sendPacket(PacketType::LifeResponse,
        TurnHubProtocol::encodeLifeResponse(lifeRequest.target, right, lifeRequest.tag));
    lifeRequest = TurnHubProtocol::LifeRequestFields{};  // Atlas resends if it still stands.
    lifeKeyRouted[k] = true;
    publishLifeOverlay();
    return true;
  }
  const bool free = sigilMenu.lifeOffered() &&
      (TURNHUB_DISPLAY_OLED ? !sigilMenu.listOpen()
          : TurnHubSigil::SigilMenu::compassAction(sigilMenu.actions(), key) == TurnHubSigil::MENU_NONE);
  const uint8_t player = shownPlayer();
  if (!free || player == 0) return false;
  lifeAdjuster.press(right ? 1 : -1, player, nowMs);
  lifeKeyRouted[k] = true;
  return true;
}

// Repeats a held life key, sends the batched total once it settles, and
// shows the running total on the ring (and the OLED).
void updateLife(uint32_t nowMs) {
  if (!sigilMenu.lifeOffered() && lifeAdjuster.pending() != 0) {
    Serial.println("SIGIL|LIFE|DROPPED|NOT_OFFERED");
    lifeAdjuster.cancel();
  }
  int32_t delta = 0;
  uint8_t player = 0;
  if (lifeAdjuster.update(nowMs, delta, player)) {
    Serial.print("SIGIL|");
    Serial.print(sigilId);
    Serial.print("|LIFE|ADJUST|");
    Serial.print(player);
    Serial.print("|");
    Serial.println(delta);
    sendPacket(PacketType::LifeAdjust, TurnHubProtocol::encodeLifeAdjust(player, delta));
  }
  ledModel.setLifePending(lifeAdjuster.pending());
  publishLifeOverlay();
}

// Five-key input: key edges go to the menu, and a finished choice becomes a
// SelectAction. Hold progress drives the status light.
void updateMenuKeys() {
  const uint32_t nowMs = millis();
  sigilMenu.setHoldTimes(static_cast<uint16_t>(longPressMs), static_cast<uint16_t>(winHoldMs));
  for (uint8_t k = 0; k < TurnHubSigil::KEY_COUNT; ++k) {
    if (!debouncedEdge(keys[k], nowMs)) continue;
    // Nothing reaches a lost Atlas; the screen says so instead.
    if (atlasLink.lost()) continue;
    const auto key = static_cast<TurnHubSigil::Key>(k);
#if TURNHUB_PICKER
    if (pickerActive) {
      // Keys choose on the picker's page, never a menu action.
      if (keys[k].stableState == LOW) {
        portENTER_CRITICAL(&displayProfileMux);
        const TurnHubProtocol::ProfilePickerPacket page = pendingPicker;
        uint8_t cursor = pickerCursor;
        portEXIT_CRITICAL(&displayProfileMux);
#if TURNHUB_DISPLAY_OLED
        // List navigation: Up/Down move locally; a choice becomes the key
        // the compass would have sent.
        TurnHubProtocol::PickerKeyCode code = TurnHubProtocol::PickerKeyCode::Left;
        const bool choose = TurnHubSigil::pickerListKey(page, cursor, key, code);
        portENTER_CRITICAL(&displayProfileMux);
        const bool moved = cursor != pickerCursor;
        pickerCursor = cursor;
        if (moved) pickerChanged = true;
        portEXIT_CRITICAL(&displayProfileMux);
        if (moved) { displayNeedsRefresh = true; notifyDisplayTask(); }
        if (!choose) continue;
#else
        (void)cursor;
        const auto code = static_cast<TurnHubProtocol::PickerKeyCode>(k);
#endif
        Serial.print("SIGIL|");
        Serial.print(sigilId);
        Serial.print("|PICKER|KEY|");
        Serial.println(static_cast<unsigned>(code));
        sendPacket(PacketType::PickerKey, TurnHubProtocol::encodePickerKey(code, page.revision));
      } else {
        sigilMenu.keyUp(key, nowMs);
      }
      continue;
    }
#endif
    if (routeLifeKey(key, keys[k].stableState == LOW, nowMs)) continue;
    if (keys[k].stableState == LOW) {
      sigilMenu.keyDown(key, nowMs);
    } else {
      sigilMenu.keyUp(key, nowMs);
    }
  }
  updateLife(nowMs);
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
  if (pairingActive ||
      pairingV2.state() == TurnHubSecureLink::SigilPairing::State::AwaitingConfirm) {
    return;
  }

  pairingToken = static_cast<int32_t>(esp_random());
  // A fresh key pair per press (about 0.2 s of X25519 on the ESP32).
  if (!pairingV2.begin(linkCrypto, pairingToken, pairRequest2)) {
    Serial.println("SIGIL|PAIR|V2|KEY_ERROR");
    return;
  }
  lastPairRequestMs = millis() - HELLO_INTERVAL_MS;
  pairingStartMs = millis();
  pairingActive = true;
  ledModel.setPairing(true, pairingStartMs);
  Serial.print("SIGIL|PAIR|START|DURATION_MS|");
  Serial.println(PAIRING_DURATION_MS);
}

void updatePairing() {
  // Waiting for the owner's code check on Atlas: give up if no verdict comes.
  if (pairingV2.state() == TurnHubSecureLink::SigilPairing::State::AwaitingConfirm) {
    if (pairingV2.expired(millis())) endPairingCodeCheck("TIMEOUT");
    return;
  }
  if (!pairingActive) {
    return;
  }

  const uint32_t elapsedMs = millis() - pairingStartMs;
  if (elapsedMs >= PAIRING_DURATION_MS) {
    pairingActive = false;
    pairingV2.cancel();
    ledModel.setPairing(false, millis());
    Serial.println("SIGIL|PAIR|TIMEOUT");
    return;
  }

  if (millis() - lastPairRequestMs >= HELLO_INTERVAL_MS) {
    sendBroadcastRaw(&pairRequest2, sizeof(pairRequest2));
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
      atlasLink.start(millis());
      atlasPairKeyValid = prefs.isKey(PAIRING_KEY_V2) &&
          prefs.getBytesLength(PAIRING_KEY_V2) == TurnHubSecureLink::KEY_BYTES &&
          prefs.getBytes(PAIRING_KEY_V2, atlasPairKey, TurnHubSecureLink::KEY_BYTES) ==
              TurnHubSecureLink::KEY_BYTES;
      if (atlasPairKeyValid) {
        linkSession.configure(sigilId, atlasPairKey);
        Serial.println("SIGIL|PAIR|LOADED|SECURE");
      } else {
        // Paired before pairing v2: no key, so it can't talk to Atlas any
        // more. Forget it; the owner pairs it again.
        prefs.remove(PAIRING_KEY);
        atlasKnown = false;
        memset(atlasMac, 0, sizeof(atlasMac));
        sigilId = UNASSIGNED_SIGIL_ID;
        atlasLink.stop();
        Serial.println("SIGIL|PAIR|KEYLESS_FORGOTTEN");
      }
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
#ifndef TURNHUB_WOKWI
  // A real read of the descriptor, so the linker keeps it in the image.
  Serial.print("SIGIL|FW_PRODUCT|");
  Serial.println(*reinterpret_cast<const volatile uint8_t *>(&sigilFirmwareDescriptor.product));
#endif
  Serial.println("SIGIL|ESP_NOW|READY");
  return true;
}

#ifndef TURNHUB_WOKWI
// Halts before the display, radio or pairing start when the strap names the
// other display type: the serial line says why, and the status light (or the
// red LED) flashes red.
void checkHardwareType() {
  pinMode(HW_TYPE_STRAP_PIN, INPUT_PULLUP);
  delay(2);
  const bool boardIsOled = digitalRead(HW_TYPE_STRAP_PIN) == LOW;
  const char *board = boardIsOled ? "OLED" : "EINK";
  const char *build = TURNHUB_DISPLAY_OLED ? "OLED" : "EINK";
  if (boardIsOled == static_cast<bool>(TURNHUB_DISPLAY_OLED)) {
    Serial.print("SIGIL|HW|");
    Serial.println(board);
    return;
  }
#if TURNHUB_STATUS_RING
  TurnHubSigil::statusRingBegin(STATUS_RING_PIN);
#else
  pinMode(RED_LED, OUTPUT);
#endif
  for (bool on = true;; on = !on) {
    Serial.print("SIGIL|HW|MISMATCH|BOARD|");
    Serial.print(board);
    Serial.print("|BUILD|");
    Serial.print(build);
    Serial.println("|HALTED (GPIO4 open = E-ink, GPIO4 to GND = OLED)");
#if TURNHUB_STATUS_RING
    TurnHubSigil::LedFrame frame{};
    for (auto &pixel : frame.pixels) pixel = TurnHubSigil::Rgb(on ? 80 : 0, 0, 0);
    TurnHubSigil::statusRingShow(frame);
#else
    digitalWrite(RED_LED, on ? HIGH : LOW);
#endif
    delay(500);
  }
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
#ifndef TURNHUB_WOKWI
  checkHardwareType();
#endif

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
  runSecureLinkSelfTest();
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
#if TURNHUB_OTA
  updater.begin(SIGIL_PRODUCT,
      TurnHubFirmwarePackage::Version{TurnHubSigilFirmware::MAJOR, TurnHubSigilFirmware::MINOR,
          TurnHubSigilFirmware::PATCH},
      WIFI_CHANNEL, TurnHubSigil::UpdaterHooks{sendUpdateStatus, showUpdateProgress, endUpdateScreen});
#endif

  if (espNowReady) {
    sendHello();
  }
  Serial.println("SIGIL|READY");
}

#if !defined(TURNHUB_WOKWI)
// Bench commands typed on the serial console (the Wokwi build reads serial
// for its simulated Atlas instead). Only the display handles any today.
void readSerialCommands() {
  static char line[40];
  static uint8_t length = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      line[length] = '\0';
      if (length && !sigilDisplay.handleCommand(line)) {
        Serial.print("SIGIL|SERIAL|UNKNOWN|");
        Serial.println(line);
      }
      length = 0;
    } else if (length + 1 < sizeof(line)) {
      line[length++] = c;
    }
  }
}
#endif

void loop() {
#if !defined(TURNHUB_WOKWI)
  readSerialCommands();
#endif
  ReceivedPacket received;
  // Bounded so a packet burst cannot starve the buttons.
  for (uint8_t n = 0; receiveQueue && n < RECEIVE_QUEUE_LENGTH &&
       xQueueReceive(receiveQueue, &received, 0) == pdTRUE; ++n) {
    handleEspNowReceive(received.mac,
        received.data, received.length);
  }
#if TURNHUB_OTA
  // A new image is kept only once it has a secure session with Atlas again.
  updater.confirmBoot(linkSession.ready(), millis());
  if (updater.pending()) updater.run();  // Blocks; restarts on success.
#endif
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
  applyAtlasLinkChange(atlasLink.update(millis()));
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
