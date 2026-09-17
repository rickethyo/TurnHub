#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "protocol.h"

#ifndef SIGIL_HAS_DISPLAY
#define SIGIL_HAS_DISPLAY 0
#endif

#if SIGIL_HAS_DISPLAY
#include "sigil_display.h"
#endif

namespace {

using TurnHubProtocol::DisplayMode;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;
#if SIGIL_HAS_DISPLAY
using TurnHubSigil::SigilDisplay;
#endif

constexpr uint8_t UNASSIGNED_SIGIL_ID = 0xFF;
constexpr uint8_t WIFI_CHANNEL = 6;

constexpr uint8_t BLUE_LED = 27;
constexpr uint8_t GREEN_LED = 14;
constexpr uint8_t RED_LED = 13;
constexpr uint8_t PASS_BUTTON = 26;
constexpr uint8_t ACTION_BUTTON = 25;
constexpr uint8_t BUZZER_PIN = 33;

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t LONG_PRESS_MS = 2000;
constexpr uint32_t WIN_HOLD_MS = 5000;
constexpr uint32_t HELLO_INTERVAL_MS = 2000;
constexpr uint32_t PASS_ACK_FLASH_MS = 250;

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
#if SIGIL_HAS_DISPLAY
SigilDisplay sigilDisplay;
#endif

bool espNowReady = false;
bool atlasKnown = false;
uint8_t atlasMac[6] = {};
uint8_t sigilId = UNASSIGNED_SIGIL_ID;
uint32_t lastHelloMs = 0;
uint32_t greenFlashUntilMs = 0;
bool commandedGreen = false;
#if SIGIL_HAS_DISPLAY
volatile bool displayNeedsRefresh = false;
volatile int32_t displayPayload = 0;
#endif

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
  if (!espNowReady) {
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
  // Discovery/heartbeat stays broadcast even after Atlas is known. This keeps
  // the Sigil visible through Atlas resets and avoids interface-MAC ambiguity.
  sendPacket(PacketType::Hello, 0, true);
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

#if SIGIL_HAS_DISPLAY
void queueReadyDisplay() {
  displayPayload = TurnHubProtocol::encodeDisplayState(
      DisplayMode::Ready, 0, 0, 0);
  displayNeedsRefresh = true;
}

void updateDisplay() {
  if (!displayNeedsRefresh) {
    return;
  }

  displayNeedsRefresh = false;

  if (sigilId == UNASSIGNED_SIGIL_ID) {
    sigilDisplay.showUnpaired();
    return;
  }

  const int32_t payload = displayPayload;
  const DisplayMode mode = TurnHubProtocol::displayMode(payload);

  if (mode == DisplayMode::Joined) {
    sigilDisplay.showJoined(
        sigilId,
        TurnHubProtocol::displayPrimaryPlayer(payload),
        TurnHubProtocol::displaySecondaryPlayer(payload),
        TurnHubProtocol::displayTurnNumber(payload));
    return;
  }

  sigilDisplay.showReady(sigilId);
}
#endif

void playBuzzerPayload(int32_t value) {
  const uint16_t frequencyHz = TurnHubProtocol::toneFrequency(value);
  const uint16_t durationMs = TurnHubProtocol::toneDuration(value);

  if (frequencyHz == 0 || durationMs == 0) {
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  tone(BUZZER_PIN, frequencyHz, durationMs);
  Serial.print("SIGIL|BUZZER|");
  Serial.print(frequencyHz);
  Serial.print("|");
  Serial.println(durationMs);
}

void handleEspNowReceive(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
  if (length != sizeof(Packet)) {
    return;
  }

  Packet packet{};
  memcpy(&packet, incomingData, sizeof(packet));

  if (packet.version != TurnHubProtocol::VERSION) {
    return;
  }

  // HELLO ACKs are also the Atlas-assigned slot handshake.
  if (packet.type == PacketType::Ack &&
      packet.value == static_cast<int32_t>(PacketType::Hello)) {
    rememberAtlas(mac);

    if (sigilId != packet.sigilId) {
      sigilId = packet.sigilId;
#if SIGIL_HAS_DISPLAY
      queueReadyDisplay();
#endif
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
      digitalWrite(RED_LED, packet.value ? HIGH : LOW);
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

    case PacketType::DisplayState:
#if SIGIL_HAS_DISPLAY
      if (displayPayload != packet.value) {
        displayPayload = packet.value;
        displayNeedsRefresh = true;
      }
#endif
      break;

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

void sendAction(PacketType type, const char *name) {
  Serial.print("SIGIL|");
  Serial.print(sigilId);
  Serial.print("|");
  Serial.println(name);
  sendPacket(type);
}

void updateActionButton() {
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
      sendAction(PacketType::ActionUp, "ACTION_UP");

      if (!actionButton.longSent) {
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

  if (!actionButton.longSent && heldMs >= LONG_PRESS_MS) {
    actionButton.longSent = true;
    sendAction(PacketType::ActionLong, "ACTION_LONG");
  }

  if (!actionButton.winSent && heldMs >= WIN_HOLD_MS) {
    actionButton.winSent = true;
    sendAction(PacketType::ActionWin, "ACTION_WIN");
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

  esp_now_register_recv_cb(handleEspNowReceive);

  if (!ensurePeer(BROADCAST_MAC)) {
    return false;
  }

  Serial.print("SIGIL|MAC|");
  Serial.println(WiFi.macAddress());
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
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(PASS_BUTTON, INPUT_PULLUP);
  pinMode(ACTION_BUTTON, INPUT_PULLUP);

  analogWrite(BLUE_LED, 0);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  passButton.rawState = passButton.stableState = digitalRead(PASS_BUTTON);
  actionButton.rawState = actionButton.stableState = digitalRead(ACTION_BUTTON);

  Serial.println();
#if SIGIL_HAS_DISPLAY
  Serial.println("SIGIL|BOOT|UNASSIGNED|DISPLAY");
  sigilDisplay.begin();
  sigilDisplay.showUnpaired();
#else
  Serial.println("SIGIL|BOOT|UNASSIGNED|BASIC");
#endif

  espNowReady = startEspNow();

  if (espNowReady) {
    sendHello();
  }
}

void loop() {
  updatePassButton();
  updateActionButton();
  updateGreenFlash();
#if SIGIL_HAS_DISPLAY
  updateDisplay();
#endif

  if (espNowReady && millis() - lastHelloMs >= HELLO_INTERVAL_MS) {
    sendHello();
  }

  delay(1);
}
