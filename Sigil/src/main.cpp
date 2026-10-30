// Sigil touchscreen controller firmware. Touches become ESP-NOW requests
// for Atlas; Atlas packets drive the LVGL screen and speaker. Sigil
// decides nothing about the game (ARCHITECTURAL_INVARIANTS.md, Invariant 1):
// it only persists the minimum pairing binding needed to find Atlas again.
//
// Threads: ESP-NOW receive callbacks only enqueue packets; loop() handles
// them. LVGL rendering runs on its own task (displayTask); state shared
// with it is guarded by displayProfileMux.

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
#include "three_part_button.h"
#include "sigil_display.h"
#include "received_packet.h"
#include "pairing_v2.h"
#include "secure_session.h"
#include "secure_link_mbedtls.h"
#include "firmware_package.h"

// This build's identity, read by tools/firmware/thfw.py when it packages
// firmware.bin (FIRMWARE_UPDATES.md). USB-only until S3 OTA lands
// (TURNHUB_OTA_RETIRED).
constexpr TurnHubFirmwarePackage::Product SIGIL_PRODUCT = TurnHubFirmwarePackage::Product::Sigil;
TURNHUB_FIRMWARE_DESCRIPTOR(sigilFirmwareDescriptor, SIGIL_PRODUCT, TurnHubSigilFirmware::MAJOR,
    TurnHubSigilFirmware::MINOR, TurnHubSigilFirmware::PATCH, TurnHubProtocol::VERSION);
#include "sigil_updater.h"
#define TURNHUB_OTA 1

#include "sigil_menu.h"
#include "life_adjust.h"
#include "led_ring.h"
#include "sigil_board.h"
#include "sigil_touch.h"
#include "sigil_touch_pad.h"

namespace {

using TurnHubProtocol::DisplayMode;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;
using TurnHubSigil::SigilDisplay;

constexpr uint8_t UNASSIGNED_SIGIL_ID = 0xFF;
constexpr uint8_t WIFI_CHANNEL = 6;

using TurnHubSigilBoard::BUZZER_PIN;
constexpr uint8_t PAIR_BUTTON = TurnHubSigilBoard::PAIR_BUTTON_PIN;
// Virtual active-low inputs for the five on-screen touch buttons.
constexpr uint8_t TOUCH_KEY_BASE = 0xE0;
constexpr uint8_t KEY_PINS[] = {TOUCH_KEY_BASE, TOUCH_KEY_BASE + 1, TOUCH_KEY_BASE + 2,
    TOUCH_KEY_BASE + 3, TOUCH_KEY_BASE + 4};
constexpr uint32_t TOUCH_SAMPLE_MS = 5;
constexpr uint8_t BUZZER_CHANNEL = 7;

// Hello announces only what varies between Sigils (protocol.h). There is one
// Sigil build, so it announces nothing extra.
uint8_t deviceCapabilities() { return 0; }

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t HELLO_INTERVAL_MS = 2000;
constexpr uint32_t PAIRING_DURATION_MS = TurnHubProtocol::PAIRING_WINDOW_MS;
// LVGL label drawing needs a 12 KB display task stack.
constexpr uint32_t DISPLAY_TASK_STACK_BYTES = 12288;
constexpr uint32_t PROFILE_REQUEST_RETRY_MS = 1000;
constexpr uint8_t PROFILE_REQUEST_MAX_ATTEMPTS = 4;
constexpr uint8_t PROFILE_SLOT_MASK = 0x03;
constexpr uint8_t RECEIVE_QUEUE_LENGTH = 32;

// The saved pairing in NVS, one record: Atlas's MAC (6 bytes), this Sigil's
// ID (1) and the pair key agreed at pairing (PAIRING_AND_SECURE_LINK.md). A record of any
// other size isn't a pairing and is dropped.
constexpr char PAIRING_NAMESPACE[] = "th_pair";
constexpr char PAIRING_KEY[] = "atlas";
constexpr size_t PAIRING_RECORD_SIZE = 7 + TurnHubSecureLink::KEY_BYTES;

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
};

ButtonState pairButton(PAIR_BUTTON);
// Quick press pairs, a medium hold unpairs, a long hold factory resets.
TurnHubProtocol::ThreePartButton pairGesture;
ButtonState keys[] = {ButtonState(KEY_PINS[0]), ButtonState(KEY_PINS[1]),
    ButtonState(KEY_PINS[2]), ButtonState(KEY_PINS[3]), ButtonState(KEY_PINS[4])};
static_assert(sizeof(keys) / sizeof(keys[0]) == TurnHubSigil::KEY_COUNT, "One button per key");
// The touch-button menu; holds are shown on screen.
TurnHubSigil::SigilMenu sigilMenu;
// Menu snapshot for the display task (guarded by displayProfileMux).
TurnHubSigil::MenuView publishedMenuView;
TurnHubSigil::MenuView pendingMenuView;
bool menuViewChanged = false;
// Atlas's profile picker (ProfilePickerPacket). Latest page from Atlas
// (guarded by displayProfileMux). While open, the keys go to the picker
// (PickerKey) instead of the menu. pickerCursor is the list row
// reset on every new page.
TurnHubProtocol::CommanderFlowPacket pendingCommander{};
volatile bool commanderActive = false;
bool commanderChanged = false;
uint16_t commanderDrawnRevision = 0;
TurnHubProtocol::CommanderStage commanderDrawnStage = TurnHubProtocol::CommanderStage::Closed;
TurnHubProtocol::ProfilePickerPacket pendingPicker{};
volatile bool pickerActive = false;
bool pickerChanged = false;
uint8_t pickerCursor = 0;
uint8_t pickerDrawnRevision = 0xFF;
// Life (AdjustLife, LifeRequest): Left/Right presses batched by lifeAdjuster;
// the request shown for this Sigil's answer. lifeKeyRouted remembers which
// key-downs went to life so their releases do not reach the menu.
TurnHubSigil::LifeAdjuster lifeAdjuster;
TurnHubProtocol::LifeRequestFields lifeRequest;
uint8_t seatAvatars[2] = {0, 0};  // From SeatColor.
int32_t startingLife = 0;  // From StartingLife: sizes the life heart.
uint8_t passingPlayer = 0;  // From PassPending: whose pass is pending.
uint8_t updateNotice = 0;   // From UpdateNotice: 0 none, 1 Atlas, 2 a Sigil.
bool lifeKeyRouted[TurnHubSigil::KEY_COUNT] = {};
// Snapshot for the display task (guarded by displayProfileMux).
TurnHubSigil::LifeOverlay publishedLifeOverlay;
TurnHubSigil::LifeOverlay pendingLifeOverlay;
bool lifeOverlayChanged = false;
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
volatile bool pairingActive = false;
uint32_t pairingStartMs = 0;
int32_t pairingToken = 0;
uint32_t lastPairRequestMs = 0;
// Pairing (pairing_v2.h, PAIRING_AND_SECURE_LINK.md): the key agreement and code check,
// and the pair key for the saved Atlas.
TurnHubSecureLink::MbedtlsCrypto linkCrypto;
TurnHubSecureLink::SigilPairing pairingV2;
TurnHubSecureLink::PairRequest2Packet pairRequest2{};
uint8_t atlasPairKey[TurnHubSecureLink::KEY_BYTES] = {};
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
TurnHubTheme::Id selectedTheme = TurnHubTheme::Id::Daylight;
TurnHubTheme::Id pendingTheme = selectedTheme;
bool themeChanged = false;
bool selectedInverted = false;
bool pendingInverted = false;

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
      deviceCapabilities());
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

bool screenTestPending = false;  // Guarded by displayProfileMux.
bool screenTestBusy = false;     // Guarded by displayProfileMux.
constexpr uint32_t TOUCH_PAIR_HOLD_MS = 3000;
void startPairing();
bool touchPairHolding = false;
bool touchPairUsed = false;  // This press already opened a window.
uint32_t touchPairStartMs = 0;

void publishMenuView();
void publishLifeOverlay();

TurnHubSigil::Key touchKey = TurnHubSigil::Key::Count;
uint32_t lastTouchSampleMs = 0;
TurnHubUi::Page selectedPage = TurnHubUi::Page::Home;
bool navigationChanged = true;
uint8_t tablePage = 0;
TurnHubSigil::TableSnapshot tableSnapshot;
void notifyDisplayTask();
uint8_t shownPlayer();

void updateTouch(uint32_t nowMs) {
  if (nowMs - lastTouchSampleMs < TOUCH_SAMPLE_MS) return;
  lastTouchSampleMs = nowMs;
  const auto input = TurnHubSigil::sigilTouchRead();
  const uint8_t target = static_cast<uint8_t>(input);
  static uint8_t previous = TurnHubSigil::TOUCH_NONE;
  touchKey = target < TurnHubSigil::KEY_COUNT ? input : TurnHubSigil::Key::Count;
  if (pickerActive && pickerDrawnRevision != pendingPicker.revision)
    touchKey = TurnHubSigil::Key::Count;
  if (selectedPage == TurnHubUi::Page::Game && !pickerActive && !commanderActive &&
      TurnHubSigil::sigilTouchMenuRevision() != sigilMenu.revision())
    touchKey = TurnHubSigil::Key::Count;
  if (selectedPage == TurnHubUi::Page::Game && gameDisplayValid && !pickerActive && !commanderActive &&
      TurnHubSigil::sigilTouchPlayer() != shownPlayer()) touchKey = TurnHubSigil::Key::Count;
  const uint16_t request = static_cast<uint16_t>(lifeRequest.target << 8) | lifeRequest.tag;
  if (selectedPage == TurnHubUi::Page::Game && gameDisplayValid && !pickerActive && !commanderActive &&
      TurnHubSigil::sigilTouchLifeRequest() != request) touchKey = TurnHubSigil::Key::Count;
  if (target != previous && target >= TurnHubSigil::TOUCH_NAV) {
    if (target < TurnHubSigil::TOUCH_NAV + TurnHubUi::PAGE_COUNT) {
      const auto page = static_cast<TurnHubUi::Page>(target - TurnHubSigil::TOUCH_NAV);
      sigilMenu.cancelInput();
      lifeAdjuster.release(nowMs);
      for (bool &routed : lifeKeyRouted) routed = false;
      if (page == TurnHubUi::Page::Settings) sigilMenu.openDeviceMenu(nowMs);
      portENTER_CRITICAL(&displayProfileMux);
      selectedPage = page; navigationChanged = true;
      portEXIT_CRITICAL(&displayProfileMux);
    } else if (target == TurnHubSigil::TOUCH_PREV || target == TurnHubSigil::TOUCH_NEXT) {
      portENTER_CRITICAL(&displayProfileMux);
      if (target == TurnHubSigil::TOUCH_PREV && tablePage) --tablePage;
      if (target == TurnHubSigil::TOUCH_NEXT && tablePage + 1 < (tableSnapshot.total + 2) / 3) ++tablePage;
      navigationChanged = true;
      portEXIT_CRITICAL(&displayProfileMux);
    }
    publishMenuView();
    displayNeedsRefresh = true; notifyDisplayTask();
  }
  previous = target;
}

bool readButton(uint8_t pin) {
  if (pin >= TOUCH_KEY_BASE && pin < TOUCH_KEY_BASE + TurnHubSigil::KEY_COUNT) {
    return static_cast<uint8_t>(touchKey) == pin - TOUCH_KEY_BASE ? LOW : HIGH;
  }
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

void loadDisplayTheme() {
  Preferences prefs;
  if (prefs.begin("display", true)) {
    const uint8_t stored = prefs.getUChar("theme", static_cast<uint8_t>(selectedTheme));
    selectedInverted = prefs.getBool("inverted", false);
    prefs.end();
    if (TurnHubTheme::valid(stored)) selectedTheme = static_cast<TurnHubTheme::Id>(stored);
  }
  pendingTheme = selectedTheme;
  pendingInverted = selectedInverted;
  sigilDisplay.setInverted(selectedInverted);
  sigilDisplay.setTheme(selectedTheme);
}

void cycleDisplayTheme() {
  const auto next = TurnHubTheme::next(selectedTheme);
  Preferences prefs;
  if (!prefs.begin("display", false)) { Serial.println("SIGIL|THEME|STORE_ERROR"); return; }
  const bool saved = prefs.putUChar("theme", static_cast<uint8_t>(next)) == 1;
  prefs.end();
  if (!saved) { Serial.println("SIGIL|THEME|STORE_ERROR"); return; }
  selectedTheme = next;
  portENTER_CRITICAL(&displayProfileMux);
  pendingTheme = next;
  themeChanged = true;
  menuViewChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

void toggleDisplayInversion() {
  const bool next = !selectedInverted;
  Preferences prefs;
  if (!prefs.begin("display", false)) { Serial.println("SIGIL|THEME|STORE_ERROR"); return; }
  const bool saved = prefs.putBool("inverted", next) == 1;
  prefs.end();
  if (!saved) { Serial.println("SIGIL|THEME|STORE_ERROR"); return; }
  selectedInverted = next;
  portENTER_CRITICAL(&displayProfileMux);
  pendingInverted = next;
  themeChanged = true;
  menuViewChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
  displayNeedsRefresh = true;
  notifyDisplayTask();
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
  portENTER_CRITICAL(&displayProfileMux);
  const auto page = selectedPage;
  const bool changedNavigation = navigationChanged;
  navigationChanged = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (changedNavigation) { renderedGameValid = false; displayNeedsRefresh = true; }
  static bool pickerShown = false;
  static bool commanderShown = false;
  if (changedNavigation) { pickerShown = false; commanderShown = false; }
  portENTER_CRITICAL(&displayProfileMux);
  const bool changedTheme = themeChanged;
  const auto theme = pendingTheme;
  const bool inverted = pendingInverted;
  themeChanged = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (changedTheme) {
    sigilDisplay.setTheme(theme);
    sigilDisplay.setInverted(inverted);
    renderedGameValid = false;
    pickerShown = false;
    commanderShown = false;
  }
  // Atlas lost replaces every screen. Once it answers again, redraw the last
  // state it sent (Atlas resends it too, but unchanged packets draw nothing).
  static bool lostDrawn = false;
  static bool codeDrawn = false;
  if (changedTheme || changedNavigation) { lostDrawn = false; codeDrawn = false; }
  static uint16_t drawnCode = 0;
  bool updateEnded = false;
#if TURNHUB_OTA
  // A firmware update outranks every screen.
  static bool updateDrawn = false;
  if (changedTheme) updateDrawn = false;
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
  portENTER_CRITICAL(&displayProfileMux);
  const bool testScreen = screenTestPending;
  screenTestPending = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (testScreen) {
    sigilDisplay.showHardwareTest();
    portENTER_CRITICAL(&displayProfileMux);
    screenTestBusy = false;
    portEXIT_CRITICAL(&displayProfileMux);
    renderedGameValid = false;
    pickerShown = false;
    commanderShown = false;
    lostDrawn = false;
    codeDrawn = false;
    displayNeedsRefresh = true;
    updateEnded = true;
  }
  if (pairingCodeShown) {
    displayNeedsRefresh = false;
    const uint16_t code = pairingCodeValue;
    if (!codeDrawn || code != drawnCode) sigilDisplay.showPairingCode(code);
    codeDrawn = true;
    drawnCode = code;
    return;
  }
  if (page != TurnHubUi::Page::Game) {
    if (!displayNeedsRefresh && !changedNavigation && !changedTheme) return;
    displayNeedsRefresh = false;
    TurnHubSigil::MenuView menu;
    TurnHubSigil::LifeOverlay life;
    TurnHubSigil::TableSnapshot roster;
    char player[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
    uint8_t rosterPage, mode;
    portENTER_CRITICAL(&displayProfileMux);
    menu = pendingMenuView; life = pendingLifeOverlay; roster = tableSnapshot; rosterPage = tablePage;
    const int32_t state = gameDisplayValid ? static_cast<int32_t>(pendingGameDisplay.state) : static_cast<int32_t>(displayPayload);
    mode = static_cast<uint8_t>(TurnHubProtocol::displayMode(state));
    snprintf(player, sizeof(player), "%s", gameDisplayValid ? pendingGameDisplay.primary.name : pendingSeatNames[0]);
    portEXIT_CRITICAL(&displayProfileMux);
    sigilDisplay.setMenuView(menu); sigilDisplay.setLifeOverlay(life);
    if (page == TurnHubUi::Page::Home) sigilDisplay.showHome(sigilId, atlasKnown && !atlasLostShown, player, mode);
    else if (page == TurnHubUi::Page::Table) sigilDisplay.showTable(sigilId, roster, rosterPage, atlasKnown && !atlasLostShown);
    else sigilDisplay.showSettings(sigilId);
    return;
  }
  // After the code or Atlas lost, redraw whatever comes next in full.
  bool overlayEnded = codeDrawn || updateEnded;
  codeDrawn = false;
  if (atlasLostShown && sigilId != UNASSIGNED_SIGIL_ID) {
    displayNeedsRefresh = false;
    // Its legend offers the device menu, which replaces it while open.
    bool lostMenuChanged = false;
    TurnHubSigil::MenuView lostMenu;
    portENTER_CRITICAL(&displayProfileMux);
    if (menuViewChanged) {
      lostMenu = pendingMenuView;
      menuViewChanged = false;
      lostMenuChanged = true;
    }
    portEXIT_CRITICAL(&displayProfileMux);
    if (lostMenuChanged) sigilDisplay.setMenuView(lostMenu);
    if (!lostDrawn || overlayEnded || lostMenuChanged) sigilDisplay.showAtlasLost(sigilId);
    lostDrawn = true;
    return;
  }
  overlayEnded = overlayEnded || lostDrawn;
  if (overlayEnded) {
    lostDrawn = false;
    renderedGameValid = false;
    pickerShown = false;
    displayNeedsRefresh = true;
  }
  bool menuChanged = false;
  // The picker replaces every other screen while Atlas keeps it open.
  TurnHubProtocol::CommanderFlowPacket commander{};
  portENTER_CRITICAL(&displayProfileMux);
  const bool showCommander = commanderActive && sigilId != UNASSIGNED_SIGIL_ID;
  const bool newCommander = commanderChanged;
  commander = pendingCommander; commanderChanged = false;
  portEXIT_CRITICAL(&displayProfileMux);
  if (showCommander) {
    displayNeedsRefresh = false;
    if (newCommander || !commanderShown) sigilDisplay.showCommander(commander);
    portENTER_CRITICAL(&displayProfileMux);
    commanderDrawnRevision = commander.revision; commanderDrawnStage = commander.stage;
    portEXIT_CRITICAL(&displayProfileMux);
    commanderShown = true;
    return;
  }
  if (commanderShown) { commanderShown = false; displayNeedsRefresh = true; renderedGameValid = false; }

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
    if (newPage || !pickerShown) {
      sigilDisplay.showPicker(picker, cursor);
      portENTER_CRITICAL(&displayProfileMux);
      pickerDrawnRevision = picker.revision;
      portEXIT_CRITICAL(&displayProfileMux);
    }
    pickerShown = true;
    return;
  }
  if (pickerShown) {
    // Closed: redraw whatever Atlas says is current.
    pickerShown = false;
    displayNeedsRefresh = true;
  }
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
    // Wait for state changes or LVGL timer work.
    const uint32_t due = sigilDisplay.idleWorkDueInMs(millis());
    if (ulTaskNotifyTake(pdTRUE, due == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(due)) == 0) {
      sigilDisplay.idleWork(millis());
      continue;
    }
    // Coalesce state, game and menu packets into one frame.
    vTaskDelay(pdMS_TO_TICKS(20));
    updateDisplay();

    // A newer display packet may have arrived while drawing.
    // Render the newest state immediately instead of waiting for another event.
    if (displayNeedsRefresh) {
      xTaskNotifyGive(displayTaskHandle);
    }
  }
}

// The touch Sigil's speaker amp: on only while a tone plays (active low).
void setAmp(bool on) {
  if (TurnHubSigilBoard::AUDIO_ENABLE_PIN >= 0) digitalWrite(TurnHubSigilBoard::AUDIO_ENABLE_PIN, on ? LOW : HIGH);
}

void stopBuzzer() {
  ledcWriteTone(BUZZER_CHANNEL, 0);
  setAmp(false);
  buzzerStopAtMs = 0;
}

void playBuzzerPayload(int32_t value) {
  const uint16_t frequencyHz = TurnHubProtocol::toneFrequency(value);
  const uint16_t durationMs = TurnHubProtocol::toneDuration(value);

  if (frequencyHz == 0 || durationMs == 0) {
    stopBuzzer();
    return;
  }

  setAmp(true);
  ledcWriteTone(BUZZER_CHANNEL, frequencyHz);
  buzzerStopAtMs = millis() + durationMs;
  Serial.print("SIGIL|BUZZER|");
  Serial.print(frequencyHz);
  Serial.print("|");
  Serial.println(durationMs);
}

// Redraws the LED ring (led_ring.h) every 20 ms from the state on screen.
void updateRing() {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if (now - lastMs < 20) return;
  lastMs = now;
  TurnHubSigil::RingInput in;
  in.paired = atlasKnown;
  in.atlasLost = atlasLink.lost();
  in.pairing = pairingActive;
  portENTER_CRITICAL(&displayProfileMux);
  in.state = gameDisplayValid ? static_cast<int32_t>(pendingGameDisplay.state) : static_cast<int32_t>(displayPayload);
  portEXIT_CRITICAL(&displayProfileMux);
  TurnHubSigil::RingPixel frame[TurnHubSigil::RING_MAX_LEDS];
  TurnHubSigil::renderRing(in, now, frame, TurnHubSigilBoard::LED_RING_COUNT);
  TurnHubSigil::ringShow(frame, TurnHubSigilBoard::LED_RING_COUNT);
}

void updateBuzzer() {
  if (buzzerStopAtMs == 0) {
    return;
  }

  if (static_cast<int32_t>(millis() - buzzerStopAtMs) >= 0) {
    stopBuzzer();
  }
}

// Erases everything saved (pairing included) and restarts as a new Sigil: an
// Admin's Factory reset in Device Settings, or the Pair button held to
// FACTORY_RESET_HOLD_MS.
void factoryResetDevice(const char *reason) {
  Serial.print("SIGIL|FACTORY_RESET|ERASING|");
  Serial.println(reason);
  Serial.flush();
  nvs_flash_erase();
  delay(100);
  ESP.restart();
}

// Erases the saved Atlas pairing and returns to the unpaired screen, from a
// medium Pair hold, the device menu's Unpair or Atlas's Unpair. Atlas keeps its own record until an admin
// forgets this Sigil there (which also sends Unpair if the Sigil is in range).
void forgetPairing(const char *reason) {
  portENTER_CRITICAL(&displayProfileMux);
  tableSnapshot.clear();
  portEXIT_CRITICAL(&displayProfileMux);
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
  atlasLink.stop();
  atlasLostShown = false;
  pairingV2.cancel();
  pairingCodeShown = false;
  TurnHubSecureLink::wipe(atlasPairKey, sizeof(atlasPairKey));
  linkSession.clear();
  sigilMenu.clear();
  lifeAdjuster.cancel();
  commanderActive = false; commanderChanged = true;
  lifeRequest = TurnHubProtocol::LifeRequestFields{};
  seatAvatars[0] = seatAvatars[1] = 0;
  startingLife = 0;
  passingPlayer = 0;
  updateNotice = 0;
  portENTER_CRITICAL(&displayProfileMux);
  pickerActive = false;
  pickerChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
  stopBuzzer();
  portENTER_CRITICAL(&displayProfileMux);
  gameDisplayValid = false;
  portEXIT_CRITICAL(&displayProfileMux);
  displayNeedsRefresh = true;
  notifyDisplayTask();
  Serial.print("SIGIL|PAIR|FORGOTTEN|");
  Serial.println(reason);
}

// Secure-link crypto check against published vectors (PAIRING_AND_SECURE_LINK.md), which
// the radio depends on; logged so each board's result is on record.
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
  Serial.print("SIGIL|PAIR|V2|");
  Serial.println(reason);
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

// Atlas confirmed the code: save Atlas, the slot and the pair key, then carry
// on paired.
void storePairingV2(const uint8_t *mac, uint8_t slot, const uint8_t *key) {
  uint8_t record[PAIRING_RECORD_SIZE];
  memcpy(record, mac, 6);
  record[6] = slot;
  memcpy(record + 7, key, TurnHubSecureLink::KEY_BYTES);
  Preferences prefs;
  bool stored = false;
  if (prefs.begin(PAIRING_NAMESPACE, false)) {
    stored = prefs.putBytes(PAIRING_KEY, record, sizeof(record)) == sizeof(record);
    prefs.end();
  }
  TurnHubSecureLink::wipe(record, sizeof(record));
  if (!stored) {
    endPairingCodeCheck("STORE_ERROR");
    return;
  }
  memcpy(atlasPairKey, key, TurnHubSecureLink::KEY_BYTES);
  linkSession.configure(slot, key);
  pairingCodeShown = false;
  rememberAtlas(mac);
  sigilId = slot;
  atlasLink.start(millis());
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
    // Stale: nothing here can reach Atlas. Atlas resends the menu and any
    // life request with its next Hello answer; an unsent life change is
    // dropped rather than applied late. Only the device menu stays, so a
    // Sigil whose Atlas is gone can still be unpaired or reset.
    sigilMenu.setOffline();
    lifeAdjuster.cancel();
    commanderActive = false; commanderChanged = true;
    lifeRequest = TurnHubProtocol::LifeRequestFields{};
  } else {
    sigilMenu.endOffline();
  }
  atlasLostShown = lost;
  Serial.println(lost ? "SIGIL|ATLAS|LOST" : "SIGIL|ATLAS|RESTORED");
  displayNeedsRefresh = true;
  notifyDisplayTask();
}

void noteAtlasHeard() {
  lastAtlasFrameMs = millis();
  applyAtlasLinkChange(atlasLink.heard(millis()));
}

#if TURNHUB_OTA
void sendUpdateStatus(int32_t value) {
  sendPacket(PacketType::SigilUpdateStatus, value);
}

// Called from updater.run(); the display task renders progress.
void showUpdateProgress(const char *status, int8_t percent) {
  static char shownText[sizeof(updateScreenText)] = {};
  static int8_t shownBucket = -2;
  // Redraw in five-percent steps.
  const int8_t bucket = percent < 0 ? -1 : static_cast<int8_t>(percent / 5);
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

}

void endUpdateScreen() {
  portENTER_CRITICAL(&displayProfileMux);
  updateScreenActive = false;
  updateScreenChanged = true;
  portEXIT_CRITICAL(&displayProfileMux);
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

// A packet from Atlas that opened in the current session (handleEspNowReceive).
void handleAtlasPacket(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
#if TURNHUB_OTA
  // Told apart by length, like the picker page below.
  if (length == sizeof(TurnHubProtocol::SigilUpdateOfferPacket)) {
    handleUpdateOffer(mac, incomingData);
    return;
  }
#endif
  if (length == sizeof(TurnHubProtocol::CommanderFlowPacket)) {
    TurnHubProtocol::CommanderFlowPacket page{};
    memcpy(&page,incomingData,sizeof(page));
    if (!atlasKnown || memcmp(mac,atlasMac,6) || page.sigilId != sigilId || !TurnHubProtocol::validCommanderFlow(page)) return;
    noteAtlasHeard();
    portENTER_CRITICAL(&displayProfileMux);
    const bool changed = memcmp(&page,&pendingCommander,sizeof(page)) != 0;
    pendingCommander = page;
    commanderActive = page.stage != TurnHubProtocol::CommanderStage::Closed;
    commanderChanged = commanderChanged || changed;
    portEXIT_CRITICAL(&displayProfileMux);
    if (changed) { displayNeedsRefresh = true; notifyDisplayTask(); }
    return;
  }
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
  if (length == sizeof(TurnHubProtocol::TableDisplayPacket)) {
    TurnHubProtocol::TableDisplayPacket roster{};
    memcpy(&roster, incomingData, sizeof(roster));
    if (!atlasKnown || memcmp(mac, atlasMac, 6) || roster.sigilId != sigilId ||
        !TurnHubProtocol::validTableDisplay(roster)) return;
    noteAtlasHeard();
    portENTER_CRITICAL(&displayProfileMux);
    const bool changed = tableSnapshot.accept(roster);
    if (changed && tablePage >= (tableSnapshot.total + 2) / 3) tablePage = 0;
    portEXIT_CRITICAL(&displayProfileMux);
    if (changed) {
      Serial.printf("SIGIL|TABLE|SNAPSHOT|GAME|%u|PLAYERS|%u\n", roster.game, roster.total);
      displayNeedsRefresh = true; notifyDisplayTask();
    }
    return;
  }
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

      break;

    case PacketType::MenuState2:
      sigilMenu.applyMenuState2(packet.value, millis());
      break;
    case PacketType::LifeRequest:
      lifeRequest = TurnHubProtocol::decodeLifeRequest(packet.value);
      break;
    case PacketType::SeatColor: {
      const uint8_t slot = TurnHubProtocol::seatColorSlot(packet.value);
      if (slot == 1 || slot == 2) seatAvatars[slot - 1] = TurnHubProtocol::seatAvatar(packet.value);
      break;
    }
    case PacketType::StartingLife:
      startingLife = constrain(packet.value, 0, 1000000);
      break;
    case PacketType::PassPending:
      passingPlayer = static_cast<uint8_t>(constrain(packet.value, 0, 16));
      break;
    case PacketType::UpdateNotice:
      updateNotice = static_cast<uint8_t>(constrain(packet.value, 0, 2));
      break;

    case PacketType::Buzzer:
      playBuzzerPayload(packet.value);
      break;

    case PacketType::HardwareTest:
      if (packet.value == 1) {
        portENTER_CRITICAL(&displayProfileMux);
        const bool busy = screenTestBusy;
        if (!busy) { screenTestPending = true; screenTestBusy = true; }
        portEXIT_CRITICAL(&displayProfileMux);
        if (busy) break;
        sigilMenu.cancelInput();
        lifeAdjuster.cancel();
        for (bool &routed : lifeKeyRouted) routed = false;
        publishMenuView();
        publishLifeOverlay();

        notifyDisplayTask();
      }
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
      factoryResetDevice("ATLAS");
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

// Hands the display task a new menu snapshot when what it shows changed.
void publishMenuView() {
  TurnHubSigil::MenuView view = sigilMenu.view();
  // Quantize progress to limit full screen rebuilds while a finger is held.
  view.holdPercent = static_cast<uint8_t>((sigilMenu.holdProgress(millis()) * 20u / 255u) * 5u);
  const uint8_t pairProgress = touchPairHolding ? static_cast<uint8_t>(
      min<uint32_t>(100u, (millis() - touchPairStartMs) * 100u / TOUCH_PAIR_HOLD_MS)) : 0;
  view.pairPercent = static_cast<uint8_t>((pairProgress / 5u) * 5u);
  view.pairing = pairingActive;
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
// The touchscreen shows the pending running total before Atlas commits it.
void publishLifeOverlay() {
  TurnHubSigil::LifeOverlay overlay;
  overlay.pending = lifeAdjuster.pending();
  overlay.pendingPlayer = lifeAdjuster.player();
  overlay.request = lifeRequest;
  overlay.avatar[0] = seatAvatars[0];
  overlay.avatar[1] = seatAvatars[1];
  overlay.startingLife = startingLife;
  // Atlas offers Undo pass only while this Sigil's pass waits out its grace.
  overlay.passPending = sigilMenu.active() && (sigilMenu.actions() &
      TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::CancelPass)) != 0;
  overlay.passingPlayer = passingPlayer;
  overlay.update = updateNotice;
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
// change life when no menu action has the key.
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
  // An open Device menu keeps its keys: Left goes back.
  if (lifeRequest.target != 0 && !sigilMenu.deviceMenuOpen()) {
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
  const bool free = sigilMenu.lifeOffered() && sigilMenu.keyAction(key) == TurnHubSigil::MENU_NONE;
  const uint8_t player = shownPlayer();
  if (!free || player == 0) return false;
  lifeAdjuster.press(right ? 1 : -1, player, nowMs);
  lifeKeyRouted[k] = true;
  return true;
}

// Repeats a held life key, sends the batched total once it settles, and
// previews the pending total on the touchscreen.
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
  publishLifeOverlay();
}

// Hold the on-screen Pair button while unpaired to open the pairing window.
// One window per touch; BOOT remains available for physical recovery.
uint8_t updateTouchPair(uint32_t nowMs) {
  const bool down = keys[static_cast<uint8_t>(TurnHubSigil::Key::Select)].stableState == LOW;
  if (!down) touchPairUsed = false;
  const bool eligible = sigilId == UNASSIGNED_SIGIL_ID && !pairingActive && !sigilMenu.deviceMenuOpen() &&
      pairingV2.state() != TurnHubSecureLink::SigilPairing::State::AwaitingConfirm;
  if (!down || !eligible || touchPairUsed) {
    touchPairHolding = false;
    return 0;
  }
  if (!touchPairHolding) {
    touchPairHolding = true;
    touchPairStartMs = nowMs;
  }
  const uint32_t elapsed = nowMs - touchPairStartMs;
  if (elapsed >= TOUCH_PAIR_HOLD_MS) {
    touchPairHolding = false;
    touchPairUsed = true;
    Serial.println("SIGIL|PAIR|TOUCH_HOLD");
    startPairing();
    return 0;
  }
  const uint32_t level = elapsed * 255 / TOUCH_PAIR_HOLD_MS;
  return static_cast<uint8_t>(level == 0 ? 1 : level);
}

// Five-key input: key edges go to the menu, and a finished choice becomes a
// SelectAction. Hold progress is drawn on screen.
void sendCommanderKey(uint8_t key, bool repeat = false) {
  portENTER_CRITICAL(&displayProfileMux);
  const auto page = pendingCommander;
  const bool drawn = commanderDrawnRevision == page.revision && commanderDrawnStage == page.stage;
  const bool amountDrawn = commanderDrawnStage == TurnHubProtocol::CommanderStage::Amount &&
      page.stage == TurnHubProtocol::CommanderStage::Amount;
  portEXIT_CRITICAL(&displayProfileMux);
  // Confirm only a preview that finished drawing. Once the
  // Amount stage is on screen, Left/Right taps and holds go straight through
  // without waiting for another frame; Select still waits, and
  // the Confirm page must be drawn before the hit is applied.
  if (!drawn && !(amountDrawn && (repeat || key == 2 || key == 3))) return;
  sendPacket(PacketType::CommanderKey,TurnHubProtocol::encodeCommanderKey(key,page.revision));
}
void updateMenuKeys() {
  static uint32_t commanderRepeatAt = 0;
  static uint8_t commanderRepeatKey = 255;
  const uint32_t nowMs = millis();
  if (selectedPage == TurnHubUi::Page::Home || selectedPage == TurnHubUi::Page::Table) {
    for (auto &key : keys) debouncedEdge(key, nowMs);
    updateLife(nowMs);
    return;
  }
  const bool settings = selectedPage == TurnHubUi::Page::Settings;
  if (settings && !sigilMenu.deviceMenuOpen() && !TurnHubSigil::sigilTouchCalibrating())
    sigilMenu.openDeviceMenu(nowMs);
  portENTER_CRITICAL(&displayProfileMux);
  const bool testing = screenTestBusy;
  portEXIT_CRITICAL(&displayProfileMux);
  static bool waitForRelease = false;
  if (testing) waitForRelease = true;
  if (waitForRelease) {
    bool released = true;
    for (uint8_t k = 0; k < TurnHubSigil::KEY_COUNT; ++k) {
      debouncedEdge(keys[k], nowMs);
      if (keys[k].rawState == LOW || keys[k].stableState == LOW) released = false;
    }
    commanderRepeatKey = 255;
    if (!testing && released) waitForRelease = false;
    return;
  }
  if (sigilId == UNASSIGNED_SIGIL_ID && !sigilMenu.active()) sigilMenu.setOffline();
  sigilMenu.setHoldTimes(static_cast<uint16_t>(longPressMs), static_cast<uint16_t>(winHoldMs));
  lifeAdjuster.setPace(TurnHubSigil::lifePaceFor(longPressMs));
  lifeAdjuster.setUnit(TurnHubSigil::lifeUnitFor(startingLife));
  for (uint8_t k = 0; k < TurnHubSigil::KEY_COUNT; ++k) {
    if (!debouncedEdge(keys[k], nowMs)) continue;
    const auto key = static_cast<TurnHubSigil::Key>(k);
    if (settings) {
      if (keys[k].stableState == LOW) sigilMenu.keyDown(key, nowMs);
      else sigilMenu.keyUp(key, nowMs);
      continue;
    }
    // Nothing reaches a lost Atlas; only the device menu works (setOffline).
    if (atlasLink.lost()) {
      if (keys[k].stableState == LOW) sigilMenu.keyDown(key, nowMs);
      else sigilMenu.keyUp(key, nowMs);
      continue;
    }
    if (commanderActive) {
      if (keys[k].stableState == LOW) {
        sendCommanderKey(k);
        portENTER_CRITICAL(&displayProfileMux);
        const bool amount = pendingCommander.stage == TurnHubProtocol::CommanderStage::Amount;
        portEXIT_CRITICAL(&displayProfileMux);
        if (amount && (k == 2 || k == 3)) { commanderRepeatKey = k; commanderRepeatAt = nowMs + 600; }
      } else {
        if (commanderRepeatKey == k) commanderRepeatKey = 255;
        sigilMenu.keyUp(key,nowMs);
      }
      continue;
    }
    if (pickerActive) {
      // Keys choose on the picker's page, never a menu action.
      if (keys[k].stableState == LOW) {
        portENTER_CRITICAL(&displayProfileMux);
        const TurnHubProtocol::ProfilePickerPacket page = pendingPicker;
        uint8_t cursor = pickerCursor;
        portEXIT_CRITICAL(&displayProfileMux);
        (void)cursor;
        const auto code = static_cast<TurnHubProtocol::PickerKeyCode>(k);
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
    if (routeLifeKey(key, keys[k].stableState == LOW, nowMs)) continue;
    if (keys[k].stableState == LOW) {
      sigilMenu.keyDown(key, nowMs);
    } else {
      sigilMenu.keyUp(key, nowMs);
    }
  }
  if (commanderActive && !atlasLink.lost() && !settings) {
    // Stop any normal life adjustment while the modal owns the keys.
    if (commanderRepeatKey < TurnHubSigil::KEY_COUNT && keys[commanderRepeatKey].stableState == LOW &&
        static_cast<int32_t>(nowMs-commanderRepeatAt) >= 0) {
      sendCommanderKey(commanderRepeatKey,true); commanderRepeatAt = nowMs+250;
    }
    return;
  }
  commanderRepeatKey = 255;
  updateLife(nowMs);
  const TurnHubSigil::MenuChoice choice = sigilMenu.update(nowMs);
  if (choice.ready && static_cast<uint8_t>(choice.action) == TurnHubSigil::MENU_LOCAL_THEME) {
    cycleDisplayTheme();
  } else if (choice.ready && static_cast<uint8_t>(choice.action) == TurnHubSigil::MENU_LOCAL_INVERT) {
    toggleDisplayInversion();
  } else if (choice.ready && static_cast<uint8_t>(choice.action) == TurnHubSigil::MENU_LOCAL_CALIBRATE) {
    TurnHubSigil::sigilTouchStartCalibration();
  } else if (choice.ready && static_cast<uint8_t>(choice.action) == TurnHubSigil::MENU_LOCAL_UNPAIR) {
    // Held to the end in the device menu: forget Atlas, like the Pair button's
    // 3 s hold. Atlas keeps its record until an admin forgets this Sigil.
    Serial.println("SIGIL|MENU|UNPAIR");
    forgetPairing("MENU");
    playBuzzerPayload(TurnHubProtocol::encodeTone(880, 200));
  } else if (choice.ready && static_cast<uint8_t>(choice.action) == TurnHubSigil::MENU_LOCAL_FACTORY_RESET) {
    // Held to the end in the device menu: erase this Sigil. Atlas is not asked.
    Serial.println("SIGIL|MENU|FACTORY_RESET");
    playBuzzerPayload(TurnHubProtocol::encodeTone(440, 300));
    delay(300);
    factoryResetDevice("MENU");
  } else if (choice.ready) {
    if ((choice.action == TurnHubProtocol::SigilAction::CommanderDamage ||
        choice.action == TurnHubProtocol::SigilAction::UndoCommanderHit) && lifeAdjuster.pending()) {
      sendPacket(PacketType::LifeAdjust,TurnHubProtocol::encodeLifeAdjust(lifeAdjuster.player(),lifeAdjuster.pending()));
      lifeAdjuster.cancel();
      for (bool &routed : lifeKeyRouted) routed = false;
      publishLifeOverlay();
    }
    Serial.print("SIGIL|");
    Serial.print(sigilId);
    Serial.print("|MENU|");
    Serial.println(TurnHubSigil::sigilActionLabel(choice.action));
    sendPacket(PacketType::SelectAction, TurnHubProtocol::encodeSelectAction(choice.action, choice.revision, shownPlayer()));
  }
  updateTouchPair(nowMs);
  publishMenuView();
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
    Serial.println("SIGIL|PAIR|TIMEOUT");
    return;
  }

  if (millis() - lastPairRequestMs >= HELLO_INTERVAL_MS) {
    sendBroadcastRaw(&pairRequest2, sizeof(pairRequest2));
    lastPairRequestMs = millis();
  }
}

// One button, three gestures (three_part_button.h): a quick press opens the
// pairing window when released, holding to UNPAIR_HOLD_MS (3 s) erases the
// saved pairing, holding to FACTORY_RESET_HOLD_MS (10 s) erases everything.
// A short tone marks each hold gesture, so release when it sounds to stop
// there; the screen shows the result too (Unpaired, then the boot screen).
void updatePairButton() {
  const uint32_t nowMs = millis();
  debouncedEdge(pairButton, nowMs);
  switch (pairGesture.update(pairButton.stableState == LOW, nowMs)) {
    case TurnHubProtocol::ButtonGesture::Pair:
      Serial.println("SIGIL|PAIR|BUTTON");
      startPairing();
      break;
    case TurnHubProtocol::ButtonGesture::Unpair:
      Serial.println("SIGIL|PAIR|BUTTON|HOLD_UNPAIR");
      forgetPairing("BUTTON");
      playBuzzerPayload(TurnHubProtocol::encodeTone(880, 200));
      break;
    case TurnHubProtocol::ButtonGesture::FactoryReset:
      Serial.println("SIGIL|PAIR|BUTTON|HOLD_FACTORY_RESET");
      playBuzzerPayload(TurnHubProtocol::encodeTone(440, 300));
      delay(300);
      factoryResetDevice("BUTTON");
      break;
    case TurnHubProtocol::ButtonGesture::None:
      break;
  }
}

void loadSavedPairing() {
  Preferences prefs;
  if (prefs.begin(PAIRING_NAMESPACE, false)) {
    uint8_t record[PAIRING_RECORD_SIZE];
    if (prefs.isKey(PAIRING_KEY)) {
      if (prefs.getBytesLength(PAIRING_KEY) == sizeof(record) &&
          prefs.getBytes(PAIRING_KEY, record, sizeof(record)) == sizeof(record) &&
          record[6] < TurnHubProtocol::MAX_SIGILS) {
        // Read before the first screen, but register the peer only after
        // ESP-NOW starts. A saved pairing stays paired even if the radio fails.
        memcpy(atlasMac, record, sizeof(atlasMac));
        atlasKnown = true;
        sigilId = record[6];
        memcpy(atlasPairKey, record + 7, TurnHubSecureLink::KEY_BYTES);
        linkSession.configure(sigilId, atlasPairKey);
        atlasLink.start(millis());
        Serial.println("SIGIL|PAIR|LOADED|SECURE");
      } else {
        prefs.remove(PAIRING_KEY);
        Serial.println("SIGIL|PAIR|BAD_RECORD_DROPPED");
      }
      TurnHubSecureLink::wipe(record, sizeof(record));
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
  // A real read of the descriptor, so the linker keeps it in the image.
  Serial.print("SIGIL|FW_PRODUCT|");
  Serial.println(*reinterpret_cast<const volatile uint8_t *>(&sigilFirmwareDescriptor.product));
  Serial.println("SIGIL|ESP_NOW|READY");
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  TurnHubSigil::sigilTouchBegin();
  Serial.println("SIGIL|TOUCH|READY");
  for (auto &key : keys) key.rawState = key.stableState = HIGH;

  if (TurnHubSigilBoard::AUDIO_ENABLE_PIN >= 0) pinMode(TurnHubSigilBoard::AUDIO_ENABLE_PIN, OUTPUT);
  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  stopBuzzer();
  TurnHubSigil::ringBegin();

  Serial.println();
  Serial.println("SIGIL|BOOT|UNASSIGNED|TOUCHSCREEN");
  runSecureLinkSelfTest();
  loadSavedPairing();
  loadDisplayTheme();
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

// Bench calibration and display commands typed on the serial console.
void readSerialCommands() {
  static char line[40];
  static uint8_t length = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      line[length] = '\0';
      if (length && strcmp(line, "touch cal") == 0) {
        TurnHubSigil::sigilTouchStartCalibration();
      } else if (length && !sigilDisplay.handleCommand(line)) {
        Serial.print("SIGIL|SERIAL|UNKNOWN|");
        Serial.println(line);
      }
      length = 0;
    } else if (length + 1 < sizeof(line)) {
      line[length++] = c;
    }
  }
}

void loop() {
  readSerialCommands();
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
  updateTouch(millis());
  updateMenuKeys();
  updatePairButton();
  updatePairing();
  applyAtlasLinkChange(atlasLink.update(millis()));

  updateBuzzer();
  updateRing();
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
