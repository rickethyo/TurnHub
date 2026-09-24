#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <freertos/queue.h>

#include "firmware_version.h"
#include "protocol.h"
#include "sigil_display.h"
#include "received_packet.h"

namespace {

using TurnHubProtocol::DisplayMode;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;
using TurnHubSigil::SigilDisplay;

constexpr uint8_t UNASSIGNED_SIGIL_ID = 0xFF;
constexpr uint8_t WIFI_CHANNEL = 6;

constexpr uint8_t BLUE_LED = 27;
constexpr uint8_t GREEN_LED = 14;
constexpr uint8_t RED_LED = 13;
constexpr uint8_t PASS_BUTTON = 26;
constexpr uint8_t ACTION_BUTTON = 25;
constexpr uint8_t PAUSE_WIN_BUTTON = 32; // Breadboard J13; switch to GND.
constexpr uint8_t BUZZER_PIN = 33;
constexpr uint8_t BUZZER_CHANNEL = 7;
constexpr uint8_t PAIR_BUTTON = 19;

// All production Sigils use the same firmware and standard display hardware.
constexpr uint8_t DEVICE_CAPABILITIES =
    TurnHubProtocol::CAPABILITY_DISPLAY |
    TurnHubProtocol::CAPABILITY_DISPLAY_PROFILE |
    TurnHubProtocol::CAPABILITY_GAME_DISPLAY;

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t LONG_PRESS_MS = 2000;
constexpr uint32_t WIN_HOLD_MS = 5000;
constexpr uint32_t HELLO_INTERVAL_MS = 2000;
constexpr uint32_t PASS_ACK_FLASH_MS = 250;
constexpr uint32_t PAIRING_DURATION_MS = 30000;
constexpr uint32_t PAIRING_BLINK_MS = 250;
constexpr uint32_t DISPLAY_TASK_STACK_BYTES = 4096;
constexpr uint32_t PROFILE_REQUEST_RETRY_MS = 1000;
constexpr uint8_t PROFILE_REQUEST_MAX_ATTEMPTS = 4;
constexpr uint8_t PROFILE_SLOT_MASK = 0x03;

constexpr uint8_t BROADCAST_MAC[6] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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
SigilDisplay sigilDisplay;

bool espNowReady = false;
bool atlasKnown = false;
uint8_t atlasMac[6] = {};
volatile uint8_t sigilId = UNASSIGNED_SIGIL_ID;
uint32_t lastHelloMs = 0;
uint32_t greenFlashUntilMs = 0;
uint32_t buzzerStopAtMs = 0;
bool commandedGreen = false;
volatile bool commandedRed = false;
volatile bool pairingActive = false;
uint32_t pairingStartMs = 0;
int32_t pairingToken = 0;
uint32_t lastPairRequestMs = 0;
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

void flashGreenForPassAck() {
  digitalWrite(GREEN_LED, HIGH);
  greenFlashUntilMs = millis() + PASS_ACK_FLASH_MS;
}

void updateGreenFlash() {
  if (greenFlashUntilMs == 0) {
    return;
  }

  if (static_cast<int32_t>(millis() - greenFlashUntilMs) >= 0) {
    greenFlashUntilMs = 0;
    digitalWrite(GREEN_LED, commandedGreen ? HIGH : LOW);
  }
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
    uint8_t binding[7];
    memcpy(binding, mac, 6);
    binding[6] = packet.sigilId;
    Preferences prefs;
    if (!prefs.begin("th_pair_v1", false)) return;
    const bool stored = prefs.putBytes("atlas", binding, sizeof(binding)) == sizeof(binding);
    prefs.end();
    if (!stored) { Serial.println("SIGIL|PAIR|STORE_ERROR"); return; }
    rememberAtlas(mac);
    sigilId = packet.sigilId;
    pairingActive = false;
    digitalWrite(RED_LED, commandedRed ? HIGH : LOW);
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
        flashGreenForPassAck();
      }
      break;

    case PacketType::SetBlue:
      analogWrite(BLUE_LED, constrain(packet.value, 0, 255));
      break;

    case PacketType::SetRed:
      commandedRed = packet.value != 0;
      if (!pairingActive) {
        digitalWrite(RED_LED, commandedRed ? HIGH : LOW);
      }
      break;

    case PacketType::SetGreen:
      commandedGreen = packet.value != 0;
      if (greenFlashUntilMs == 0) {
        digitalWrite(GREEN_LED, commandedGreen ? HIGH : LOW);
      }
      break;

    case PacketType::Buzzer:
      playBuzzerPayload(packet.value);
      break;

    case PacketType::DisplayProfileRequest:
      profileSyncStartPending = true;
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

void updatePassButton() {
  const bool reading = digitalRead(passButton.pin);

  if (reading != passButton.rawState) {
    passButton.rawState = reading;
    passButton.lastDebounceMs = millis();
  }

  if (millis() - passButton.lastDebounceMs < DEBOUNCE_MS) {
    return;
  }

  if (passButton.stableState == passButton.rawState) {
    return;
  }

  passButton.stableState = passButton.rawState;

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
  digitalWrite(RED_LED, HIGH);
  Serial.println("SIGIL|PAIR|START|DURATION_MS|30000");
}

void updatePairing() {
  if (!pairingActive) {
    return;
  }

  const uint32_t elapsedMs = millis() - pairingStartMs;
  if (elapsedMs >= PAIRING_DURATION_MS) {
    pairingActive = false;
    digitalWrite(RED_LED, commandedRed ? HIGH : LOW);
    Serial.println("SIGIL|PAIR|TIMEOUT");
    return;
  }

  if (millis() - lastPairRequestMs >= HELLO_INTERVAL_MS) {
    sendPacket(PacketType::PairRequest, pairingToken, true);
    lastPairRequestMs = millis();
  }
  digitalWrite(
      RED_LED, (elapsedMs / PAIRING_BLINK_MS) % 2 == 0 ? HIGH : LOW);
}

void updatePairButton() {
  const bool reading = digitalRead(pairButton.pin);
  const uint32_t nowMs = millis();

  if (reading != pairButton.rawState) {
    pairButton.rawState = reading;
    pairButton.lastDebounceMs = nowMs;
  }

  if (nowMs - pairButton.lastDebounceMs < DEBOUNCE_MS ||
      pairButton.stableState == pairButton.rawState) {
    return;
  }

  pairButton.stableState = pairButton.rawState;
  if (pairButton.stableState == LOW) {
    Serial.println("SIGIL|PAIR|BUTTON");
    startPairing();
  } else {
    Serial.println("SIGIL|PAIR|BUTTON|UP");
  }
}

void sendAction(PacketType type, const char *name) {
  Serial.print("SIGIL|");
  Serial.print(sigilId);
  Serial.print("|");
  Serial.println(name);
  sendPacket(type);
}

void updateActionButton(ButtonState &actionButton, bool pauseWin = false) {
  const bool reading = digitalRead(actionButton.pin);

  if (reading != actionButton.rawState) {
    actionButton.rawState = reading;
    actionButton.lastDebounceMs = millis();
  }

  if (millis() - actionButton.lastDebounceMs >= DEBOUNCE_MS &&
      actionButton.stableState != actionButton.rawState) {
    actionButton.stableState = actionButton.rawState;

    if (actionButton.stableState == LOW) {
      actionButton.pressStartMs = millis();
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

  const uint32_t heldMs = millis() - actionButton.pressStartMs;

  // A dedicated win hold arms the claim immediately before sending it,
  // rather than pausing at the original Action button's 2-second threshold.
  if (!actionButton.longSent && heldMs >= (pauseWin ? WIN_HOLD_MS : LONG_PRESS_MS)) {
    actionButton.longSent = true;
    sendAction(PacketType::ActionLong, "ACTION_LONG");
  }

  if (!actionButton.winSent && heldMs >= WIN_HOLD_MS) {
    actionButton.winSent = true;
    sendAction(PacketType::ActionWin, "ACTION_WIN");
  }
}

void loadSavedPairing() {
  Preferences prefs;
  if (prefs.begin("th_pair_v1", false)) {
    uint8_t binding[7];
    if (prefs.isKey("atlas") && prefs.getBytesLength("atlas") == sizeof(binding) &&
        prefs.getBytes("atlas", binding, sizeof(binding)) == sizeof(binding) &&
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

  receiveQueue = xQueueCreate(32, sizeof(ReceivedPacket));
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
  pinMode(ACTION_BUTTON, INPUT_PULLUP);
  pinMode(PAUSE_WIN_BUTTON, INPUT_PULLUP);

  analogWrite(BLUE_LED, 0);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);

  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  stopBuzzer();

  passButton.rawState = passButton.stableState = digitalRead(PASS_BUTTON);
  actionButton.rawState = actionButton.stableState = digitalRead(ACTION_BUTTON);
  pauseWinButton.rawState = pauseWinButton.stableState = digitalRead(PAUSE_WIN_BUTTON);

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
  for (uint8_t n = 0; receiveQueue && n < 32 &&
       xQueueReceive(receiveQueue, &received, 0) == pdTRUE; ++n) {
    handleEspNowReceive(received.mac,
        received.data, received.length);
  }
  updatePassButton();
  updateActionButton(actionButton);
  updateActionButton(pauseWinButton, true);
  updatePairButton();
  updatePairing();
  updateGreenFlash();
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
