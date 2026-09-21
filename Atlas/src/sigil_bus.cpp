#include "sigil_bus.h"

#include <WiFi.h>
#include <cstring>

#include "profile_store.h"
#include "web_api.h"

namespace TurnHub {

using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;

namespace {

String displaySafeName(const String &name) {
  String safe;
  safe.reserve(TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH);
  for (size_t i = 0;
       i < name.length() && safe.length() < TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH;
       ++i) {
    const uint8_t c = static_cast<uint8_t>(name[i]);
    safe += (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
  }
  return safe;
}

}  // namespace

SigilBus *SigilBus::instance_ = nullptr;

SigilBus::SigilBus(uint8_t wifiChannel)
    : wifiChannel_(wifiChannel) {}

SigilBus *SigilBus::activeInstance() {
  return instance_;
}

bool SigilBus::begin() {
  instance_ = this;

  eventQueue_ = xQueueCreate(32, sizeof(SigilEvent));
  txQueue_ = xQueueCreate(48, sizeof(TxRequest));
  if (eventQueue_ == nullptr || txQueue_ == nullptr) {
    Serial.println("ATLAS|SIGIL_BUS|QUEUE_ERROR");
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ATLAS|ESP_NOW|ERROR");
    return false;
  }

  esp_now_register_recv_cb(receiveThunk);
  esp_now_register_send_cb(sendThunk);

  if (xTaskCreate(
          txTaskThunk,
          "atlas_espnow_tx",
          3072,
          this,
          2,
          &txTask_) != pdPASS) {
    txTask_ = nullptr;
    Serial.println("ATLAS|ESP_NOW|TX_TASK_ERROR");
    return false;
  }

  Serial.println("ATLAS|ESP_NOW|READY");
  return true;
}

bool SigilBus::poll(SigilEvent &event) {
  if (eventQueue_ == nullptr) {
    return false;
  }

  if (xQueueReceive(eventQueue_, &event, 0) != pdTRUE) {
    return false;
  }

  // Keep profile/NVS work out of the ESP-NOW callback. A Sigil asks for its
  // display profile over ESP-NOW, then the main-loop consumer reads the
  // durable profile bound to that seat and queues response packets here.
  if (event.physical && event.type == PacketType::DisplayProfileRequest) {
    syncDisplayProfile(event.sigilId);
  }

  // Physical confirmation is deliberately handled in the main-loop consumer,
  // not inside the ESP-NOW receive callback. That keeps web-session state on a
  // single execution path and prevents synthetic browser events from proving
  // possession of a Sigil.
  if (event.physical && event.type == PacketType::ActionDown) {
    TurnHubWebApi::notePhysicalAction(event.sigilId);
  }

  return true;
}

uint8_t SigilBus::activeCount(uint32_t nowMs) const {
  uint8_t count = 0;
  for (const auto &sigil : records_) {
    if (sigil.used && nowMs - sigil.lastSeenMs <= SIGIL_TIMEOUT_MS) {
      ++count;
    }
  }
  return count;
}

bool SigilBus::isOnline(uint8_t sigilId, uint32_t nowMs) const {
  const SigilRecord *sigil = record(sigilId);
  return sigil != nullptr && nowMs - sigil->lastSeenMs <= SIGIL_TIMEOUT_MS;
}

const SigilRecord *SigilBus::record(uint8_t sigilId) const {
  if (sigilId >= MAX_PHYSICAL_SIGILS || !records_[sigilId].used) {
    return nullptr;
  }
  return &records_[sigilId];
}

bool SigilBus::setBlue(uint8_t sigilId, uint8_t brightness) {
  return send(sigilId, PacketType::SetBlue, brightness);
}

bool SigilBus::setRed(uint8_t sigilId, bool on) {
  return send(sigilId, PacketType::SetRed, on ? 1 : 0);
}

bool SigilBus::setGreen(uint8_t sigilId, bool on) {
  return send(sigilId, PacketType::SetGreen, on ? 1 : 0);
}

bool SigilBus::buzzer(uint8_t sigilId, int32_t value) {
  return send(sigilId, PacketType::Buzzer, value);
}

bool SigilBus::send(uint8_t sigilId, PacketType type, int32_t value) {
  const SigilRecord *sigil = record(sigilId);
  if (sigil == nullptr) {
    return false;
  }
  return sendToMac(sigil->mac, type, sigilId, value);
}

bool SigilBus::injectEvent(uint8_t sigilId, PacketType type, int32_t value) {
  if (eventQueue_ == nullptr || !isOnline(sigilId, millis())) {
    return false;
  }

  SigilEvent event;
  event.sigilId = sigilId;
  event.type = type;
  event.value = value;
  event.physical = false;
  return xQueueSend(eventQueue_, &event, 0) == pdTRUE;
}

void SigilBus::receiveThunk(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
  if (instance_ != nullptr) {
    instance_->handleReceive(mac, incomingData, length);
  }
}

void SigilBus::sendThunk(
    const uint8_t *mac,
    esp_now_send_status_t status) {
  (void)mac;
  (void)status;

  if (instance_ != nullptr && instance_->txTask_ != nullptr) {
    xTaskNotifyGive(instance_->txTask_);
  }
}

void SigilBus::txTaskThunk(void *context) {
  auto *bus = static_cast<SigilBus *>(context);
  if (bus != nullptr) {
    bus->txTaskLoop();
  }
  vTaskDelete(nullptr);
}

void SigilBus::txTaskLoop() {
  TxRequest request;

  for (;;) {
    if (xQueueReceive(txQueue_, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    uint8_t retry = 0;
    for (;;) {
      ulTaskNotifyTake(pdTRUE, 0);

      const esp_err_t result = esp_now_send(
          request.mac,
          reinterpret_cast<const uint8_t *>(&request.packet),
          sizeof(request.packet));

      if (result == ESP_OK) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250)) == 0) {
          Serial.println("ATLAS|ESP_NOW|SEND_TIMEOUT");
        }
        break;
      }

      if (result == ESP_ERR_ESPNOW_NO_MEM && retry < 4) {
        ++retry;
        vTaskDelay(pdMS_TO_TICKS(2U * retry));
        continue;
      }

      Serial.print("ATLAS|ESP_NOW|SEND_ERROR|");
      Serial.println(static_cast<int>(result));
      break;
    }
  }
}

void SigilBus::handleReceive(
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

  SigilRecord *sigil = remember(mac);
  if (sigil == nullptr) {
    Serial.println("ATLAS|SIGIL|TABLE_FULL");
    return;
  }

  sigil->lastSeenMs = millis();

  switch (packet.type) {
    case PacketType::Hello:
      updateHelloInfo(*sigil, packet.value);
      sendAck(mac, *sigil, packet.type);
      enqueue(*sigil, packet);
      break;

    case PacketType::Pass:
    case PacketType::ActionDown:
    case PacketType::ActionUp:
    case PacketType::ActionShort:
    case PacketType::ActionLong:
    case PacketType::ActionWin:
    case PacketType::DisplayProfileRequest:
      sendAck(mac, *sigil, packet.type);
      enqueue(*sigil, packet);
      break;

    default:
      break;
  }
}

SigilRecord *SigilBus::findByMac(const uint8_t *mac) {
  for (auto &sigil : records_) {
    if (sigil.used && memcmp(sigil.mac, mac, 6) == 0) {
      return &sigil;
    }
  }
  return nullptr;
}

SigilRecord *SigilBus::remember(const uint8_t *mac) {
  SigilRecord *existing = findByMac(mac);
  if (existing != nullptr) {
    return existing;
  }

  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
    SigilRecord &candidate = records_[i];
    if (candidate.used) {
      continue;
    }

    candidate.used = true;
    candidate.id = i;
    memcpy(candidate.mac, mac, 6);
    candidate.lastSeenMs = millis();

    Serial.print("ATLAS|SIGIL|DISCOVERED|");
    Serial.print(candidate.id);
    Serial.print("|THS-");
    for (uint8_t byte : candidate.mac) {
      Serial.printf("%02X", byte);
    }
    Serial.print("|");
    printMac(candidate.mac);
    Serial.println();
    return &candidate;
  }

  return nullptr;
}

void SigilBus::updateHelloInfo(SigilRecord &sigil, int32_t value) {
  if (value == 0) {
    sigil.helloInfoValid = false;
    sigil.firmwareMajor = 0;
    sigil.firmwareMinor = 0;
    sigil.firmwarePatch = 0;
    sigil.capabilities = 0;
    return;
  }

  const uint8_t major = TurnHubProtocol::helloFirmwareMajor(value);
  const uint8_t minor = TurnHubProtocol::helloFirmwareMinor(value);
  const uint8_t patch = TurnHubProtocol::helloFirmwarePatch(value);
  const uint8_t capabilities = TurnHubProtocol::helloCapabilities(value);

  const bool changed =
      !sigil.helloInfoValid ||
      sigil.firmwareMajor != major ||
      sigil.firmwareMinor != minor ||
      sigil.firmwarePatch != patch ||
      sigil.capabilities != capabilities;

  sigil.helloInfoValid = true;
  sigil.firmwareMajor = major;
  sigil.firmwareMinor = minor;
  sigil.firmwarePatch = patch;
  sigil.capabilities = capabilities;

  if (!changed) {
    return;
  }

  Serial.print("ATLAS|SIGIL|INFO|");
  Serial.print(sigil.id);
  Serial.print("|THS-");
  for (uint8_t byte : sigil.mac) {
    Serial.printf("%02X", byte);
  }
  Serial.print("|FW|");
  Serial.print(static_cast<unsigned>(major));
  Serial.print(".");
  Serial.print(static_cast<unsigned>(minor));
  Serial.print(".");
  Serial.print(static_cast<unsigned>(patch));
  Serial.print("|CAPS|0x");
  Serial.println(static_cast<unsigned>(capabilities), HEX);
}

bool SigilBus::ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = wifiChannel_;
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

bool SigilBus::sendToMac(
    const uint8_t *mac,
    PacketType type,
    uint8_t sigilId,
    int32_t value) {
  if (txQueue_ == nullptr || !ensurePeer(mac)) {
    return false;
  }

  TxRequest request;
  memcpy(request.mac, mac, 6);
  request.packet = TurnHubProtocol::makePacket(type, sigilId, value);

  if (xQueueSend(txQueue_, &request, 0) != pdTRUE) {
    Serial.println("ATLAS|ESP_NOW|TX_QUEUE_FULL");
    return false;
  }

  return true;
}

void SigilBus::sendAck(
    const uint8_t *mac,
    const SigilRecord &sigil,
    PacketType receivedType) {
  sendToMac(
      mac,
      PacketType::Ack,
      sigil.id,
      static_cast<int32_t>(receivedType));
}

void SigilBus::enqueue(
    const SigilRecord &sigil,
    const Packet &packet) {
  if (eventQueue_ == nullptr) {
    return;
  }

  SigilEvent event;
  event.sigilId = sigil.id;
  event.type = packet.type;
  event.value = packet.value;
  event.physical = true;
  xQueueSend(eventQueue_, &event, 0);
}

bool SigilBus::sendDisplayName(
    uint8_t sigilId,
    uint8_t slot,
    const String &name) {
  if (slot != 1 && slot != 2) {
    return false;
  }

  const String safe = displaySafeName(name);
  const uint8_t length = static_cast<uint8_t>(safe.length());
  const uint8_t chunkCount = length == 0
      ? 1
      : static_cast<uint8_t>(
          (length + TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS - 1) /
          TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS);

  bool ok = true;
  for (uint8_t chunk = 0; chunk < chunkCount; ++chunk) {
    const uint8_t offset = chunk * TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS;
    char chars[TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS] = {};
    for (uint8_t i = 0; i < TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS; ++i) {
      const uint8_t index = offset + i;
      if (index < length) {
        chars[i] = safe[index];
      }
    }

    const int32_t payload = TurnHubProtocol::encodeDisplayNameChunk(
        slot,
        chunk,
        chunk + 1 == chunkCount,
        chars[0],
        chars[1],
        chars[2]);
    ok = send(sigilId, PacketType::DisplayNameChunk, payload) && ok;
  }
  return ok;
}

void SigilBus::syncDisplayProfile(uint8_t sigilId) {
  const SigilRecord *sigil = record(sigilId);
  if (sigil == nullptr) {
    return;
  }

  if (!TurnHubProfiles::ready() && !TurnHubProfiles::begin()) {
    Serial.println("ATLAS|DISPLAY_PROFILE|STORE_ERROR");
    return;
  }

  const String nameA = TurnHubProfiles::nameForSeat(sigil->mac, 1);
  const String nameB = TurnHubProfiles::nameForSeat(sigil->mac, 2);

  sendDisplayName(sigilId, 1, nameA);
  sendDisplayName(sigilId, 2, nameB);

  Serial.print("ATLAS|DISPLAY_PROFILE|SYNC|SIGIL|");
  Serial.print(sigilId);
  Serial.print("|A|");
  Serial.print(displaySafeName(nameA));
  Serial.print("|B|");
  Serial.println(displaySafeName(nameB));
}

void SigilBus::printMac(const uint8_t *mac) {
  Serial.printf(
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace TurnHub
