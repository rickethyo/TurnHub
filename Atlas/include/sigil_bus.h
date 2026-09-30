#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "pairing_v2.h"
#include "secure_session.h"
#include "protocol.h"
#include "turnhub_types.h"

namespace TurnHub {

// A decoded button/hello packet from a known Sigil, delivered by poll().
struct SigilEvent {
  uint8_t sigilId = INVALID_ID;
  TurnHubProtocol::PacketType type = TurnHubProtocol::PacketType::Hello;
  int32_t value = 0;
};

// What Atlas knows about a paired Sigil. IDs index MAX_PHYSICAL_SIGILS slots.
struct SigilRecord {
  bool used = false;
  uint8_t id = INVALID_ID;
  uint8_t mac[6] = {};
  uint32_t lastSeenMs = 0;

  uint32_t sessionGeneration = 0;
  uint32_t confirmedSessionGeneration = 0;
  bool helloInfoValid = false;
  uint8_t firmwareMajor = 0;
  uint8_t firmwareMinor = 0;
  uint8_t firmwarePatch = 0;
  uint8_t capabilities = 0;
  bool profileRequestSeen = false;
  uint32_t lastProfileRequestMs = 0;
  // The key agreed at pairing (SECURE_LINK.md), stored in NVS with the MAC.
  uint8_t pairKey[TurnHubSecureLink::KEY_BYTES] = {};
  // The current secure session (secure_session.h), started by the Sigil's
  // SecureHello. Every packet either way is sealed in it; RAM only.
  TurnHubSecureLink::AtlasSession session;
};

// ESP-NOW transport to the physical Sigils. The radio callback only queues
// packets; poll() hands them to the application loop and a dedicated task
// sends outgoing packets, so neither side blocks the other. It moves bytes
// and pairing handshakes only; it never interprets game meaning.
class SigilBus {
 public:
  explicit SigilBus(uint8_t wifiChannel);

  bool begin();
  // Opens Atlas's pairing window for windowMs (see pairing_settings.h).
  bool openPairing(uint32_t windowMs);
  void closePairing() { pairingOpen_ = false; }
  bool pairingActive() const;
  // Erases a paired Sigil's saved association and frees its slot. Sends it a
  // best-effort Unpair first so it can forget Atlas too. False when the slot
  // is unused or the store could not be updated (the record is then kept).
  bool forget(uint8_t sigilId);
  // Pairing v2: a Sigil awaiting the owner's code check in that slot, or
  // nullptr. The code is on the Sigil's screen and Atlas's.
  const TurnHubSecureLink::PendingPairing *pendingPairing(uint8_t slot) const;
  uint8_t pendingPairingCount() const;
  // The owner's verdict (PairConfirm Intent). Confirm stores the Sigil and its
  // pair key; either way the Sigil is told. False if nothing is pending there
  // or the store failed (nothing is then stored).
  bool decidePairing(uint8_t slot, bool confirm);
  // Leaving the lobby: every waiting Sigil is told no.
  void cancelPendingPairings();
  // Next received event, if any. Call from the application loop.
  bool poll(SigilEvent &event);

  uint8_t activeCount(uint32_t nowMs) const;
  bool isOnline(uint8_t sigilId, uint32_t nowMs) const;
  const SigilRecord *record(uint8_t sigilId) const;
  // Refresh names without requesting a reconnect handshake.
  void syncDisplayProfile(uint8_t sigilId);

  bool sendGameDisplay(const TurnHubProtocol::GameDisplayPacket &packet);
  bool sendUpdateOffer(const TurnHubProtocol::SigilUpdateOfferPacket &packet);
  bool sendProfilePicker(const TurnHubProtocol::ProfilePickerPacket &packet);

  bool buzzer(uint8_t sigilId, int32_t value);

  bool send(
      uint8_t sigilId,
      TurnHubProtocol::PacketType type,
      int32_t value = 0);

  // The bus constructed in main.cpp, for modules without a reference to it.
  static SigilBus *activeInstance();
  static constexpr uint32_t SIGIL_TIMEOUT_MS = TurnHubProtocol::LINK_TIMEOUT_MS;

 private:
  // Radio callbacks copy any packet Atlas accepts: a sealed 7-byte Packet, a
  // SecureHello, or a pairing v2 request (the largest).
  static constexpr uint8_t RX_MAX_BYTES = sizeof(TurnHubSecureLink::PairRequest2Packet);
  struct RxRequest {
    uint8_t mac[6];
    uint8_t data[RX_MAX_BYTES];
    uint8_t length;
    uint32_t receivedAt;
  };
  struct TxRequest {
    uint8_t mac[6] = {};
    // The largest sealed packet: an update offer plus the envelope.
    uint8_t data[sizeof(TurnHubProtocol::SigilUpdateOfferPacket) + TurnHubSecureLink::SECURE_OVERHEAD] = {};
    uint8_t length = 0;
  };

  static SigilBus *instance_;
  static void receiveThunk(
      const uint8_t *mac,
      const uint8_t *incomingData,
      int length);
  static void sendThunk(
      const uint8_t *mac,
      esp_now_send_status_t status);
  static void txTaskThunk(void *context);

  void handleReceive(
      const uint8_t *mac,
      const uint8_t *incomingData,
      int length);
  void txTaskLoop();

  SigilRecord *findByMac(const uint8_t *mac);
  void handlePairRequest2(const uint8_t *mac, const TurnHubSecureLink::PairRequest2Packet &packet,
      uint32_t receivedAt);
  bool slotFree(uint8_t slot) const;
  bool storeRecord(uint8_t slot, const uint8_t *mac, const uint8_t *pairKey);
  bool sendRaw(const uint8_t *mac, const void *data, uint8_t length);
  // Seals one packet in the Sigil's session and queues it; false (nothing
  // sent) until the Sigil has said SecureHello. App task only.
  bool sendSealed(SigilRecord &sigil, const void *inner, size_t length);
  void handleSecureHello(const uint8_t *mac, const TurnHubSecureLink::SecureHelloPacket &hello);
  void handleSealed(const uint8_t *mac, const uint8_t *frame, size_t length);
  void updateHelloInfo(SigilRecord &sigil, int32_t value);
  bool ensurePeer(const uint8_t *mac);
  bool sendToMac(
      const uint8_t *mac,
      TurnHubProtocol::PacketType type,
      uint8_t sigilId,
      int32_t value);
  void sendAck(
      const uint8_t *mac,
      const SigilRecord &sigil,
      TurnHubProtocol::PacketType receivedType);
  void enqueue(const SigilRecord &sigil, const TurnHubProtocol::Packet &packet);

  bool sendDisplayName(uint8_t sigilId, uint8_t slot, const String &name);

  static void printMac(const uint8_t *mac);

  uint8_t wifiChannel_;
  bool pairingOpen_ = false;
  uint32_t pairingStartedMs_ = 0;
  uint32_t pairingWindowMs_ = TurnHubProtocol::PAIRING_WINDOW_MS;
  QueueHandle_t rxQueue_ = nullptr;
  SigilRecord records_[MAX_PHYSICAL_SIGILS];
  TurnHubSecureLink::AtlasPairings pairings_;
  QueueHandle_t eventQueue_ = nullptr;
  QueueHandle_t txQueue_ = nullptr;
  TaskHandle_t txTask_ = nullptr;
};

}  // namespace TurnHub
