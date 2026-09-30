#include "sigil_bus.h"

#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>

#include "profile_store.h"
#include "optional_preferences.h"
#include "secure_link_mbedtls.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHub {

using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;

namespace {

// The secure link's crypto (mbedTLS; checked at boot by secure_link_backend).
TurnHubSecureLink::MbedtlsCrypto linkCrypto;

// NVS key of a slot's pair key, beside its MAC ("s<slot>"). Kept separate so
// records from before pairing v2 still load, as keyless.
String pairKeyName(uint8_t slot) { return String("k") + String(slot); }

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
    const String keyName = pairKeyName(i);
    records_[i].hasPairKey = prefs.isKey(keyName.c_str()) &&
        prefs.getBytesLength(keyName.c_str()) == TurnHubSecureLink::KEY_BYTES &&
        prefs.getBytes(keyName.c_str(), records_[i].pairKey, TurnHubSecureLink::KEY_BYTES) ==
            TurnHubSecureLink::KEY_BYTES;
    // Since the secure link every packet is sealed with the pair key, so a
    // Sigil paired before pairing v2 can't talk to Atlas: forget it (it must
    // pair again). Its MAC is logged; the pair key never is.
    if (!records_[i].hasPairKey) {
      prefs.remove(key.c_str());
      serialLog.printf("ATLAS|PAIRING|KEYLESS_FORGOTTEN|%u\n", static_cast<unsigned>(i));
      records_[i] = SigilRecord{};
    }
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
  const String keyName = pairKeyName(sigilId);
  const bool removed = (!prefs.isKey(key.c_str()) || prefs.remove(key.c_str())) &&
      (!prefs.isKey(keyName.c_str()) || prefs.remove(keyName.c_str()));
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
  TurnHubSecureLink::wipe(sigil.pairKey, sizeof(sigil.pairKey));
  sigil = SigilRecord{};
  pairings_.cancelSlot(sigilId);
  return true;
}

bool SigilBus::poll(SigilEvent &event) {
  if (pairingOpen_ && !pairingActive()) closePairing();
  RxRequest request;
  // Radio callbacks only copy packets; pairing, NVS and records belong to loop().
  for (uint8_t n = 0; rxQueue_ && n < 32 &&
       xQueueReceive(rxQueue_, &request, 0) == pdTRUE; ++n) {
    const bool inWindow = pairingActive() &&
        request.receivedAt - pairingStartedMs_ < pairingWindowMs_;
    if (request.length == sizeof(TurnHubSecureLink::PairRequest2Packet)) {
      if (!inWindow) continue;
      TurnHubSecureLink::PairRequest2Packet pairRequest;
      memcpy(&pairRequest, request.data, sizeof(pairRequest));
      handlePairRequest2(request.mac, pairRequest, request.receivedAt);
      continue;
    }
    if (request.length == sizeof(TurnHubSecureLink::SecureHelloPacket)) {
      TurnHubSecureLink::SecureHelloPacket hello;
      memcpy(&hello, request.data, sizeof(hello));
      handleSecureHello(request.mac, hello);
      continue;
    }
    handleSealed(request.mac, request.data, request.length);
  }
  // Unconfirmed pairings lapse after PAIR_CONFIRM_TIMEOUT_MS; the Sigil is told.
  TurnHubSecureLink::PairResultPacket lapsed;
  uint8_t lapsedMac[6];
  while (pairings_.expireOne(linkCrypto, millis(), lapsed, lapsedMac)) {
    sendRaw(lapsedMac, &lapsed, sizeof(lapsed));
    serialLog.printf("ATLAS|PAIRING|V2|EXPIRED|%u\n", static_cast<unsigned>(lapsed.sigilId));
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
    if ((length != sizeof(Packet) + TurnHubSecureLink::SECURE_OVERHEAD &&
         length != sizeof(TurnHubSecureLink::SecureHelloPacket) &&
         length != sizeof(TurnHubSecureLink::PairRequest2Packet)) ||
        instance_->rxQueue_ == nullptr) {
      return;
    }
    RxRequest request;
    memcpy(request.mac, mac, 6);
    memcpy(request.data, incomingData, static_cast<size_t>(length));
    request.length = static_cast<uint8_t>(length);
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

  // Only packets opened in a Sigil's session get here (handleSealed), so the
  // sender is a paired Sigil holding its pair key.
  SigilRecord *sigil = findByMac(mac);
  if (sigil == nullptr || packet.sigilId != sigil->id) {
    return;
  }
  sigil->lastSeenMs = millis();

  switch (packet.type) {
    case PacketType::Hello:
      sigil->confirmedSessionGeneration = sigil->sessionGeneration;
      updateHelloInfo(*sigil, packet.value);
      sendAck(mac, *sigil, packet.type);
      enqueue(*sigil, packet);
      break;

    case PacketType::SelectAction:
    case PacketType::PickerKey:
    case PacketType::LifeAdjust:
    case PacketType::LifeResponse:
    case PacketType::SigilUpdateStatus:
    case PacketType::HarnessReport:
    case PacketType::DisplayProfileRequest:
      sendAck(mac, *sigil, packet.type);
      enqueue(*sigil, packet);
      break;

    default:
      break;
  }
}

// A pairing v2 request during Atlas's window: agree a key and show the code.
// The Sigil keeps its slot if it was paired before; otherwise it gets a free
// one. Nothing is stored until the owner confirms (decidePairing).
void SigilBus::handlePairRequest2(const uint8_t *mac,
    const TurnHubSecureLink::PairRequest2Packet &packet, uint32_t receivedAt) {
  uint8_t slot = INVALID_ID;
  if (const SigilRecord *known = findByMac(mac)) {
    slot = known->id;
  } else {
    for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS && slot == INVALID_ID; ++i) {
      const TurnHubSecureLink::PendingPairing *pending = pairings_.findSlot(i);
      if (pending != nullptr && memcmp(pending->mac, mac, 6) == 0) slot = i;
    }
    for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS && slot == INVALID_ID; ++i) {
      if (slotFree(i)) slot = i;
    }
  }
  if (slot == INVALID_ID) {
    serialLog.println("ATLAS|PAIRING|V2|REJECT|CAPACITY");
    return;
  }
  // ESP-NOW goes out on the station interface, so this is the MAC a Sigil
  // sees (and Atlas's THA- ID).
  uint8_t atlasMac[6];
  if (esp_wifi_get_mac(WIFI_IF_STA, atlasMac) != ESP_OK) return;
  const TurnHubSecureLink::PendingPairing *existing = pairings_.findSlot(slot);
  const bool repeat = existing != nullptr && existing->token == packet.token &&
      memcmp(existing->mac, mac, 6) == 0;
  TurnHubSecureLink::PairAccept2Packet accept;
  if (pairings_.request(linkCrypto, atlasMac, mac, packet, slot, receivedAt, accept) == nullptr) {
    serialLog.println("ATLAS|PAIRING|V2|REJECT|REQUEST");
    return;
  }
  sendRaw(mac, &accept, sizeof(accept));
  if (!repeat) {
    // The code itself is shown on the screens, never logged.
    serialLog.printf("ATLAS|PAIRING|V2|CODE_SHOWN|%u|PENDING|%u\n",
        static_cast<unsigned>(slot), static_cast<unsigned>(pairings_.count()));
  }
}

const TurnHubSecureLink::PendingPairing *SigilBus::pendingPairing(uint8_t slot) const {
  return pairings_.findSlot(slot);
}

uint8_t SigilBus::pendingPairingCount() const { return pairings_.count(); }

bool SigilBus::decidePairing(uint8_t slot, bool confirm) {
  const TurnHubSecureLink::PendingPairing *pending = pairings_.findSlot(slot);
  if (pending == nullptr) return false;
  uint8_t mac[6];
  memcpy(mac, pending->mac, 6);
  TurnHubSecureLink::PairResultPacket result;
  uint8_t key[TurnHubSecureLink::KEY_BYTES] = {};
  uint8_t sigilMac[6];
  // Store before telling the Sigil: one told "confirmed" must find Atlas
  // holding its key. A failed store turns the confirm into a reject.
  if (confirm && !storeRecord(slot, pending->mac, pending->pairKey)) {
    if (pairings_.decide(linkCrypto, slot, false, result, key, sigilMac)) {
      sendRaw(mac, &result, sizeof(result));
    }
    serialLog.printf("ATLAS|PAIRING|V2|STORE_ERROR|%u\n", static_cast<unsigned>(slot));
    return false;
  }
  const bool ok = pairings_.decide(linkCrypto, slot, confirm, result, key, sigilMac);
  TurnHubSecureLink::wipe(key, sizeof(key));
  if (!ok) return false;
  sendRaw(mac, &result, sizeof(result));
  serialLog.printf("ATLAS|PAIRING|V2|%s|%u\n", confirm ? "CONFIRMED" : "REJECTED",
      static_cast<unsigned>(slot));
  return true;
}

void SigilBus::cancelPendingPairings() {
  for (uint8_t slot = 0; slot < MAX_PHYSICAL_SIGILS; ++slot) {
    if (pairings_.findSlot(slot) != nullptr) decidePairing(slot, false);
  }
}

bool SigilBus::slotFree(uint8_t slot) const {
  return !records_[slot].used && pairings_.findSlot(slot) == nullptr;
}

// Saves a confirmed Sigil in its slot: the MAC as before, plus the pair key.
bool SigilBus::storeRecord(uint8_t slot, const uint8_t *mac, const uint8_t *pairKey) {
  if (slot >= MAX_PHYSICAL_SIGILS) return false;
  SigilRecord &record = records_[slot];
  if (record.used && memcmp(record.mac, mac, 6) != 0) return false;
  OptionalPreferences prefs;
  if (!prefs.begin("th_pair_v1", false)) return false;
  const String key = String("s") + String(slot);
  const String keyName = pairKeyName(slot);
  const bool stored = prefs.putBytes(key.c_str(), mac, 6) == 6 &&
      prefs.putBytes(keyName.c_str(), pairKey, TurnHubSecureLink::KEY_BYTES) ==
          TurnHubSecureLink::KEY_BYTES;
  prefs.end();
  if (!stored) return false;
  const bool wasUsed = record.used;
  record.used = true;
  record.id = slot;
  memcpy(record.mac, mac, 6);
  record.hasPairKey = true;
  memcpy(record.pairKey, pairKey, TurnHubSecureLink::KEY_BYTES);
  if (!wasUsed) {
    record.lastSeenMs = millis() - SIGIL_TIMEOUT_MS - 1;
    serialLog.print("ATLAS|SIGIL|DISCOVERED|");
    serialLog.print(slot);
    serialLog.print("|");
    printMac(mac);
    serialLog.println("|SECURE");
  }
  return true;
}

// A SecureHello from a paired Sigil holding its pair key starts (or
// restarts) its session, and counts as its Hello: Atlas answers, notes its
// firmware, and resends its lights, menu and screen (sealed, after the ack).
void SigilBus::handleSecureHello(const uint8_t *mac,
    const TurnHubSecureLink::SecureHelloPacket &hello) {
  SigilRecord *sigil = findByMac(mac);
  if (sigil == nullptr || !sigil->hasPairKey) return;
  TurnHubSecureLink::SecureHelloAckPacket ack;
  if (!sigil->session.acceptHello(linkCrypto, sigil->pairKey, sigil->id, hello, ack)) {
    serialLog.printf("ATLAS|SECURE|HELLO_REJECTED|%u\n", static_cast<unsigned>(sigil->id));
    return;
  }
  sendRaw(mac, &ack, sizeof(ack));
  sigil->lastSeenMs = millis();
  ++sigil->sessionGeneration;
  updateHelloInfo(*sigil, hello.info);
  const Packet asHello = TurnHubProtocol::makePacket(PacketType::Hello, sigil->id, hello.info);
  enqueue(*sigil, asHello);
}

// A sealed packet from a paired Sigil: it must open in that Sigil's current
// session (right key, rising counter), or it is dropped. Opened packets go
// through the same handling as before the secure link.
void SigilBus::handleSealed(const uint8_t *mac, const uint8_t *frame, size_t length) {
  SigilRecord *sigil = findByMac(mac);
  if (sigil == nullptr) return;
  uint8_t inner[sizeof(Packet)];
  if (sigil->session.open(linkCrypto, sigil->id, frame, length, inner, sizeof(inner)) !=
      sizeof(Packet)) {
    return;  // Forged, replayed, from an old session, or before SecureHello.
  }
  handleReceive(mac, inner, sizeof(Packet));
}

bool SigilBus::sendSealed(SigilRecord &sigil, const void *inner, size_t length) {
  if (txQueue_ == nullptr || !sigil.session.ready() || !ensurePeer(sigil.mac)) return false;
  TxRequest request;
  memcpy(request.mac, sigil.mac, 6);
  const size_t sealed = sigil.session.seal(linkCrypto, sigil.id, inner, length, request.data,
      sizeof(request.data));
  if (sealed == 0) return false;
  request.length = static_cast<uint8_t>(sealed);
  if (xQueueSend(txQueue_, &request, 0) != pdTRUE) {
    serialLog.println("ATLAS|ESP_NOW|TX_QUEUE_FULL");
    return false;
  }
  return true;
}

bool SigilBus::sendRaw(const uint8_t *mac, const void *data, uint8_t length) {
  if (txQueue_ == nullptr || length > sizeof(TxRequest::data) || !ensurePeer(mac)) return false;
  TxRequest request;
  memcpy(request.mac, mac, 6);
  memcpy(request.data, data, length);
  request.length = length;
  return xQueueSend(txQueue_, &request, 0) == pdTRUE;
}

SigilRecord *SigilBus::findByMac(const uint8_t *mac) {
  for (auto &sigil : records_) {
    if (sigil.used && memcmp(sigil.mac, mac, 6) == 0) {
      return &sigil;
    }
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

  SigilRecord *sigil = findByMac(mac);
  if (sigil == nullptr) return false;
  const Packet packet = TurnHubProtocol::makePacket(type, sigilId, value);
  return sendSealed(*sigil, &packet, sizeof(packet));
}

bool SigilBus::sendGameDisplay(const TurnHubProtocol::GameDisplayPacket &packet) {
  SigilRecord *sigil = const_cast<SigilRecord *>(record(packet.sigilId));
  return sigil != nullptr && sendSealed(*sigil, &packet, sizeof(packet));
}

bool SigilBus::sendUpdateOffer(const TurnHubProtocol::SigilUpdateOfferPacket &packet) {
  SigilRecord *sigil = const_cast<SigilRecord *>(record(packet.sigilId));
  return sigil != nullptr && sendSealed(*sigil, &packet, sizeof(packet));
}

bool SigilBus::sendProfilePicker(const TurnHubProtocol::ProfilePickerPacket &packet) {
  static_assert(sizeof(packet) + TurnHubSecureLink::SECURE_OVERHEAD <= sizeof(TxRequest::data),
      "A sealed picker page must fit a TxRequest");
  SigilRecord *sigil = const_cast<SigilRecord *>(record(packet.sigilId));
  return sigil != nullptr && sendSealed(*sigil, &packet, sizeof(packet));
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
