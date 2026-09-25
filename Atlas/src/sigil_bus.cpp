#include "sigil_bus.h"

#include <WiFi.h>
#include <cstring>

#include "profile_store.h"
#include "optional_preferences.h"
#include "web_api.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHub {

using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;

namespace {

void displaySafeName(const String &name,
    char (&safe)[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1]) {
  size_t length = 0;
  for (size_t i = 0;
       i < name.length() && length < TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH;
       ++i) {
    const uint8_t c = static_cast<uint8_t>(name[i]);
    safe[length++] = (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
  }
  safe[length] = '\0';
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

  OptionalPreferences prefs;
  if (!prefs.begin("th_pair_v1", false)) return false;
  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
    const String key = String("s") + String(i);
    if (!prefs.isKey(key.c_str())) continue;
    uint8_t mac[6];
    if (prefs.getBytesLength(key.c_str()) != sizeof(mac) ||
        prefs.getBytes(key.c_str(), mac, sizeof(mac)) != sizeof(mac)) {
      prefs.end();
      serialLog.println("ATLAS|PAIRING|STORE_ERROR");
      return false;
    }
    records_[i].used = true;
    records_[i].id = i;
    memcpy(records_[i].mac, mac, 6);
    records_[i].lastSeenMs = millis() - SIGIL_TIMEOUT_MS - 1;
  }
  prefs.end();
  rxQueue_ = xQueueCreate(32, sizeof(RxRequest));
  eventQueue_ = xQueueCreate(32, sizeof(SigilEvent));
  txQueue_ = xQueueCreate(48, sizeof(TxRequest));
  if (rxQueue_ == nullptr || eventQueue_ == nullptr || txQueue_ == nullptr) {
    serialLog.println("ATLAS|SIGIL_BUS|QUEUE_ERROR");
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    serialLog.println("ATLAS|ESP_NOW|ERROR");
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
    serialLog.println("ATLAS|ESP_NOW|TX_TASK_ERROR");
    return false;
  }

  serialLog.println("ATLAS|ESP_NOW|READY");
  return true;
}

bool SigilBus::openPairing(uint32_t windowMs) {
  if (rxQueue_ == nullptr || txTask_ == nullptr) return false;
  pairingStartedMs_ = millis();
  pairingWindowMs_ = windowMs;
  pairingOpen_ = true;
  return true;
}

bool SigilBus::pairingActive() const {
  return pairingOpen_ && millis() - pairingStartedMs_ < pairingWindowMs_;
}

bool SigilBus::forget(uint8_t sigilId) {
  if (sigilId >= MAX_PHYSICAL_SIGILS || !records_[sigilId].used) return false;
  SigilRecord &sigil = records_[sigilId];

  OptionalPreferences prefs;
  const String key = String("s") + String(sigilId);
  if (!prefs.begin("th_pair_v1", false)) {
    serialLog.println("ATLAS|PAIRING|STORE_ERROR");
    return false;
  }
  const bool removed = !prefs.isKey(key.c_str()) || prefs.remove(key.c_str());
  prefs.end();
  if (!removed) {
    serialLog.println("ATLAS|PAIRING|STORE_ERROR");
    return false;
  }

  // Queued before the record goes; the TX task only needs the MAC.
  sendToMac(sigil.mac, PacketType::Unpair, sigil.id, 0);
  serialLog.print("ATLAS|SIGIL|FORGOTTEN|");
  serialLog.print(sigil.id);
  serialLog.print("|");
  printMac(sigil.mac);
  serialLog.println();
  sigil = SigilRecord{};
  return true;
}

bool SigilBus::poll(SigilEvent &event) {
  if (pairingOpen_ && !pairingActive()) closePairing();
  RxRequest request;
  // Radio callbacks only copy packets; pairing, NVS and records belong to loop().
  for (uint8_t n = 0; rxQueue_ && n < 32 &&
       xQueueReceive(rxQueue_, &request, 0) == pdTRUE; ++n) {
    if (request.packet.type == PacketType::PairRequest &&
        (!pairingActive() || request.receivedAt - pairingStartedMs_ >= pairingWindowMs_)) continue;
    handleReceive(request.mac, reinterpret_cast<const uint8_t *>(&request.packet), sizeof(Packet));
  }
  if (eventQueue_ == nullptr) {
    return false;
  }

  if (xQueueReceive(eventQueue_, &event, 0) != pdTRUE) {
    return false;
  }

  // Every queued event came from a real radio packet (enqueue() is the only
  // producer), so button events here prove physical possession of the Sigil.
  //
  // Keep profile/NVS work out of the ESP-NOW callback. A Sigil asks for its
  // display profile over ESP-NOW, then the main-loop consumer reads the
  // durable profile bound to that seat and queues response packets here.
  if (event.type == PacketType::DisplayProfileRequest) {
    SigilRecord *record = const_cast<SigilRecord *>(this->record(event.sigilId));
    const uint32_t nowMs = millis();
    if (record != nullptr &&
        (!record->profileRequestSeen || nowMs - record->lastProfileRequestMs > 5000)) {
      if (TurnHubProfiles::resetTransientSeatBindings(record->mac)) {
        serialLog.print("ATLAS|PROFILE|TRANSIENT_SEATS|CLEARED|");
        serialLog.println(event.sigilId);
      }
    }
    if (record != nullptr) {
      record->profileRequestSeen = true;
      record->lastProfileRequestMs = nowMs;
    }
    syncDisplayProfile(event.sigilId);
  }

  // Physical confirmation is deliberately handled in the main-loop consumer,
  // not inside the ESP-NOW receive callback. That keeps web-session state on a
  // single execution path and prevents synthetic browser events from proving
  // possession of a Sigil.
  if (event.type == PacketType::ActionDown) {
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

void SigilBus::receiveThunk(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
  if (instance_ != nullptr) {
    if (length != sizeof(Packet) || instance_->rxQueue_ == nullptr) return;
    RxRequest request;
    memcpy(request.mac, mac, 6);
    memcpy(&request.packet, incomingData, sizeof(Packet));
    request.receivedAt = millis();
    xQueueSend(instance_->rxQueue_, &request, 0);
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
          request.data,
          request.length);

      if (result == ESP_OK) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250)) == 0) {
          serialLog.println("ATLAS|ESP_NOW|SEND_TIMEOUT");
        }
        break;
      }

      if (result == ESP_ERR_ESPNOW_NO_MEM && retry < 4) {
        ++retry;
        vTaskDelay(pdMS_TO_TICKS(2U * retry));
        continue;
      }

      serialLog.print("ATLAS|ESP_NOW|SEND_ERROR|");
      serialLog.println(static_cast<int>(result));
      break;
    }
  }
}

void SigilBus::handleReceive(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int length) {
  if (length != sizeof(Packet)) {
    serialLog.printf("ATLAS|ESP_NOW|BAD_LENGTH|%d\n", length);
    return;
  }

  Packet packet{};
  memcpy(&packet, incomingData, sizeof(packet));

  if (packet.version != TurnHubProtocol::VERSION) {
    serialLog.printf(
        "ATLAS|ESP_NOW|BAD_VERSION|%u\n",
        static_cast<unsigned>(packet.version));
    return;
  }

  SigilRecord *sigil = findByMac(mac);
  if (packet.type == PacketType::PairRequest) {
    if (!pairingActive()) return;
    if (!sigil) sigil = remember(mac);
    if (!sigil) serialLog.println("ATLAS|PAIRING|REJECT|CAPACITY_OR_STORAGE");
    if (sigil) {
      sendToMac(mac, PacketType::PairAccept, sigil->id, packet.value);
      serialLog.printf("ATLAS|PAIRING|ACCEPT|%u\n", sigil->id);
    }
    return;
  }
  if (sigil == nullptr) {
    return;
  }

  if (packet.type != PacketType::Hello && packet.sigilId != sigil->id) return;
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
    case PacketType::SelectAction:
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

    OptionalPreferences prefs;
    const String key = String("s") + String(i);
    if (!prefs.begin("th_pair_v1", false)) return nullptr;
    const bool stored = prefs.putBytes(key.c_str(), mac, 6) == 6;
    prefs.end();
    if (!stored) {
      serialLog.println("ATLAS|PAIRING|STORE_ERROR");
      return nullptr;
    }
    candidate.used = true;
    candidate.id = i;
    memcpy(candidate.mac, mac, 6);
    candidate.lastSeenMs = millis() - SIGIL_TIMEOUT_MS - 1;

    serialLog.print("ATLAS|SIGIL|DISCOVERED|");
    serialLog.print(candidate.id);
    serialLog.print("|THS-");
    for (uint8_t byte : candidate.mac) {
      serialLog.printf("%02X", byte);
    }
    serialLog.print("|");
    printMac(candidate.mac);
    serialLog.println();
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

  serialLog.print("ATLAS|SIGIL|INFO|");
  serialLog.print(sigil.id);
  serialLog.print("|THS-");
  for (uint8_t byte : sigil.mac) {
    serialLog.printf("%02X", byte);
  }
  serialLog.print("|FW|");
  serialLog.print(static_cast<unsigned>(major));
  serialLog.print(".");
  serialLog.print(static_cast<unsigned>(minor));
  serialLog.print(".");
  serialLog.print(static_cast<unsigned>(patch));
  serialLog.print("|CAPS|0x");
  serialLog.println(static_cast<unsigned>(capabilities), HEX);
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
    serialLog.print("ATLAS|ESP_NOW|PEER_ERROR|");
    printMac(mac);
    serialLog.print("|");
    serialLog.println(static_cast<int>(result));
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
  const Packet packet = TurnHubProtocol::makePacket(type, sigilId, value);
  memcpy(request.data, &packet, sizeof(packet));
  request.length = sizeof(packet);

  if (xQueueSend(txQueue_, &request, 0) != pdTRUE) {
    serialLog.println("ATLAS|ESP_NOW|TX_QUEUE_FULL");
    return false;
  }

  return true;
}

bool SigilBus::sendGameDisplay(const TurnHubProtocol::GameDisplayPacket &packet) {
  const SigilRecord *sigil = record(packet.sigilId);
  if (!sigil || !txQueue_ || !ensurePeer(sigil->mac)) return false;
  TxRequest request;
  memcpy(request.mac, sigil->mac, 6);
  memcpy(request.data, &packet, sizeof(packet));
  request.length = sizeof(packet);
  return xQueueSend(txQueue_, &request, 0) == pdTRUE;
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
  xQueueSend(eventQueue_, &event, 0);
}

bool SigilBus::sendDisplayName(
    uint8_t sigilId,
    uint8_t slot,
    const String &name) {
  if (slot != 1 && slot != 2) {
    return false;
  }

  char safe[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1];
  displaySafeName(name, safe);
  const uint8_t length = static_cast<uint8_t>(strlen(safe));
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
    serialLog.println("ATLAS|DISPLAY_PROFILE|STORE_ERROR");
    return;
  }

  const String nameA = TurnHubProfiles::nameForSeat(sigil->mac, 1);
  const String nameB = TurnHubProfiles::nameForSeat(sigil->mac, 2);

  sendDisplayName(sigilId, 1, nameA);
  sendDisplayName(sigilId, 2, nameB);

  serialLog.print("ATLAS|DISPLAY_PROFILE|SYNC|SIGIL|");
  serialLog.print(sigilId);
  serialLog.print("|A|");
  char safe[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1];
  displaySafeName(nameA, safe);
  serialLog.print(safe);
  serialLog.print("|B|");
  displaySafeName(nameB, safe);
  serialLog.println(safe);
}

void SigilBus::printMac(const uint8_t *mac) {
  serialLog.printf(
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace TurnHub
