#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>

#include "config.h"
#include "protocol.h"

WebServer server(AtlasConfig::HTTP_PORT);

namespace {

using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>TurnHub Atlas</title>
  <style>
    :root { color-scheme: dark; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      font-family: system-ui, sans-serif;
      background: #0b0d11;
      color: #f3f5f7;
    }
    main {
      width: min(520px, calc(100% - 32px));
      padding: 28px;
      border: 1px solid #2b3240;
      border-radius: 18px;
      background: #141820;
    }
    h1 { margin-top: 0; }
    .status {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 12px;
      padding: 12px 0;
      border-bottom: 1px solid #2b3240;
    }
    .status:last-child { border-bottom: 0; }
    .value { font-weight: 800; }
    .online { color: #62d58a; }
    .pressed { color: #72a7ff; }
    .muted { color: #9ca6b7; }
  </style>
</head>
<body>
  <main>
    <h1>TurnHub Atlas</h1>
    <div class="status"><span>Atlas</span><span class="value online">Online</span></div>
    <div class="status"><span>Sigils</span><span id="sigils" class="value">0</span></div>
    <div class="status"><span>Master Button</span><span id="button" class="value muted">Released</span></div>
    <div class="status"><span>ESP-NOW</span><span id="espnow" class="value">Starting</span></div>
  </main>
  <script>
    async function refresh() {
      try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        const state = await response.json();
        const button = document.getElementById('button');
        button.textContent = state.masterButton ? 'Pressed' : 'Released';
        button.className = state.masterButton ? 'value pressed' : 'value muted';
        document.getElementById('sigils').textContent = state.sigils;
        document.getElementById('espnow').textContent = state.espNow ? 'Ready' : 'Error';
      } catch (_) {
        document.getElementById('espnow').textContent = 'Disconnected';
      }
    }
    refresh();
    setInterval(refresh, 250);
  </script>
</body>
</html>
)HTML";

struct SigilRecord {
  bool used = false;
  uint8_t id = 0;
  uint8_t mac[6] = {};
  uint32_t lastSeenMs = 0;
};

SigilRecord sigils[TurnHubProtocol::MAX_SIGILS];

bool espNowReady = false;
bool lastButtonState = HIGH;
uint32_t lastDebounceMs = 0;

constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t SIGIL_TIMEOUT_MS = 7000;

bool masterButtonPressed() {
  return digitalRead(AtlasConfig::MASTER_BUTTON_PIN) == LOW;
}

void printMac(const uint8_t *mac) {
  Serial.printf(
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

SigilRecord *findSigilByMac(const uint8_t *mac) {
  for (auto &sigil : sigils) {
    if (sigil.used && memcmp(sigil.mac, mac, 6) == 0) {
      return &sigil;
    }
  }
  return nullptr;
}

SigilRecord *rememberSigil(const uint8_t *mac, uint8_t id) {
  SigilRecord *record = findSigilByMac(mac);

  if (record == nullptr) {
    for (auto &candidate : sigils) {
      if (!candidate.used) {
        candidate.used = true;
        candidate.id = id;
        memcpy(candidate.mac, mac, 6);
        candidate.lastSeenMs = millis();
        record = &candidate;

        Serial.print("ATLAS|SIGIL|DISCOVERED|");
        Serial.print(id);
        Serial.print("|");
        printMac(mac);
        Serial.println();
        break;
      }
    }
  }

  if (record != nullptr) {
    record->id = id;
    record->lastSeenMs = millis();
  }

  return record;
}

uint8_t activeSigilCount() {
  const uint32_t now = millis();
  uint8_t count = 0;

  for (const auto &sigil : sigils) {
    if (sigil.used && now - sigil.lastSeenMs <= SIGIL_TIMEOUT_MS) {
      ++count;
    }
  }

  return count;
}

bool ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = AtlasConfig::WIFI_CHANNEL;
  peer.encrypt = false;

  const esp_err_t result = esp_now_add_peer(&peer);
  if (result != ESP_OK) {
    Serial.print("ATLAS|ESP_NOW|PEER_ERROR|");
    printMac(mac);
    Serial.print("|");
    Serial.println(static_cast<int>(result));
    return false;
  }

  return true;
}

void sendPacket(
    const uint8_t *mac,
    PacketType type,
    uint8_t sigilId,
    int32_t value = 0) {
  if (!ensurePeer(mac)) {
    return;
  }

  const Packet packet = TurnHubProtocol::makePacket(type, sigilId, value);
  const esp_err_t result = esp_now_send(
      mac,
      reinterpret_cast<const uint8_t *>(&packet),
      sizeof(packet));

  if (result != ESP_OK) {
    Serial.print("ATLAS|ESP_NOW|SEND_ERROR|");
    Serial.println(static_cast<int>(result));
  }
}

void sendAck(const uint8_t *mac, const Packet &received) {
  sendPacket(
      mac,
      PacketType::Ack,
      received.sigilId,
      static_cast<int32_t>(received.type));
}

void handleEspNowReceive(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
  if (length != sizeof(Packet)) {
    Serial.printf("ATLAS|ESP_NOW|BAD_LENGTH|%d\n", length);
    return;
  }

  Packet packet{};
  memcpy(&packet, incomingData, sizeof(packet));

  if (packet.version != TurnHubProtocol::VERSION) {
    Serial.printf(
        "ATLAS|ESP_NOW|BAD_VERSION|%u\n",
        static_cast<unsigned>(packet.version));
    return;
  }

  SigilRecord *record = rememberSigil(mac, packet.sigilId);
  if (record == nullptr) {
    Serial.println("ATLAS|SIGIL|TABLE_FULL");
    return;
  }

  switch (packet.type) {
    case PacketType::Hello:
      Serial.print("ATLAS|SIGIL|");
      Serial.print(packet.sigilId);
      Serial.println("|HELLO");
      sendAck(mac, packet);
      break;

    case PacketType::Pass:
      Serial.print("ATLAS|SIGIL|");
      Serial.print(packet.sigilId);
      Serial.println("|PASS");
      // ACKing PASS causes the test Sigil to flash green. This proves the
      // return path Atlas -> Sigil without involving game state yet.
      sendAck(mac, packet);
      break;

    case PacketType::ActionDown:
      Serial.printf("ATLAS|SIGIL|%u|ACTION_DOWN\n", packet.sigilId);
      sendAck(mac, packet);
      break;

    case PacketType::ActionUp:
      Serial.printf("ATLAS|SIGIL|%u|ACTION_UP\n", packet.sigilId);
      sendAck(mac, packet);
      break;

    case PacketType::ActionShort:
      Serial.printf("ATLAS|SIGIL|%u|ACTION_SHORT\n", packet.sigilId);
      sendAck(mac, packet);
      break;

    case PacketType::ActionLong:
      Serial.printf("ATLAS|SIGIL|%u|ACTION_LONG\n", packet.sigilId);
      sendAck(mac, packet);
      break;

    case PacketType::ActionWin:
      Serial.printf("ATLAS|SIGIL|%u|ACTION_WIN\n", packet.sigilId);
      sendAck(mac, packet);
      break;

    default:
      break;
  }
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  char json[128];
  snprintf(
      json,
      sizeof(json),
      "{\"masterButton\":%s,\"sigils\":%u,\"espNow\":%s}",
      masterButtonPressed() ? "true" : "false",
      static_cast<unsigned>(activeSigilCount()),
      espNowReady ? "true" : "false");

  server.send(200, "application/json", json);
}

void updateMasterButton() {
  const bool currentState = digitalRead(AtlasConfig::MASTER_BUTTON_PIN);

  if (currentState == lastButtonState) {
    return;
  }

  if (millis() - lastDebounceMs < DEBOUNCE_MS) {
    return;
  }

  lastDebounceMs = millis();
  lastButtonState = currentState;

  Serial.println(currentState == LOW
                     ? "ATLAS|MASTER_BUTTON|DOWN"
                     : "ATLAS|MASTER_BUTTON|UP");
}

void startNetworking() {
  WiFi.mode(WIFI_AP_STA);

  const bool apStarted = WiFi.softAP(
      AtlasConfig::WIFI_SSID,
      nullptr,
      AtlasConfig::WIFI_CHANNEL,
      false,
      8);

  if (!apStarted) {
    Serial.println("ATLAS|WIFI_AP|ERROR");
    return;
  }

  Serial.print("ATLAS|WIFI_AP|READY|");
  Serial.print(AtlasConfig::WIFI_SSID);
  Serial.print("|");
  Serial.println(WiFi.softAPIP());

  Serial.print("ATLAS|MAC|");
  Serial.println(WiFi.macAddress());

  espNowReady = esp_now_init() == ESP_OK;
  if (espNowReady) {
    esp_now_register_recv_cb(handleEspNowReceive);
  }

  Serial.println(espNowReady ? "ATLAS|ESP_NOW|READY" : "ATLAS|ESP_NOW|ERROR");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  Serial.println("ATLAS|WEB|READY");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(AtlasConfig::MASTER_BUTTON_PIN, INPUT_PULLUP);
  lastButtonState = digitalRead(AtlasConfig::MASTER_BUTTON_PIN);

  Serial.println();
  Serial.println("ATLAS|BOOT");

  startNetworking();

  Serial.println("ATLAS|READY");
}

void loop() {
  updateMasterButton();
  server.handleClient();
  delay(1);
}
