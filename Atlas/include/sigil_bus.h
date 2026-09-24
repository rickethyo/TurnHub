#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

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

  bool helloInfoValid = false;
  uint8_t firmwareMajor = 0;
  uint8_t firmwareMinor = 0;
  uint8_t firmwarePatch = 0;
  uint8_t capabilities = 0;
  bool profileRequestSeen = false;
  uint32_t lastProfileRequestMs = 0;
};

// ESP-NOW transport to the physical Sigils. The radio callback only queues
// packets; poll() hands them to the application loop and a dedicated task
// sends outgoing packets, so neither side blocks the other. It moves bytes
// and pairing handshakes only; it never interprets game meaning.
class SigilBus {
 public:
  explicit SigilBus(uint8_t wifiChannel);

  bool begin();
  bool openPairing();
  void closePairing() { pairingOpen_ = false; }
  bool pairingActive() const;
  // Next received event, if any. Call from the application loop.
  bool poll(SigilEvent &event);

  uint8_t activeCount(uint32_t nowMs) const;
  bool isOnline(uint8_t sigilId, uint32_t nowMs) const;
  const SigilRecord *record(uint8_t sigilId) const;
  // Refresh names without requesting a reconnect handshake.
  void syncDisplayProfile(uint8_t sigilId);

  bool sendGameDisplay(const TurnHubProtocol::GameDisplayPacket &packet);

  bool setBlue(uint8_t sigilId, uint8_t brightness);
  bool setRed(uint8_t sigilId, bool on);
  bool setGreen(uint8_t sigilId, bool on);
  bool buzzer(uint8_t sigilId, int32_t value);

  bool send(
      uint8_t sigilId,
      TurnHubProtocol::PacketType type,
      int32_t value = 0);

  // The bus constructed in main.cpp, for modules without a reference to it.
  static SigilBus *activeInstance();
  static constexpr uint32_t SIGIL_TIMEOUT_MS = 7000;

 private:
  struct RxRequest {
    uint8_t mac[6];
    TurnHubProtocol::Packet packet;
    uint32_t receivedAt;
  };
  struct TxRequest {
    uint8_t mac[6] = {};
    uint8_t data[sizeof(TurnHubProtocol::GameDisplayPacket)] = {};
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
  SigilRecord *remember(const uint8_t *mac);
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
  QueueHandle_t rxQueue_ = nullptr;
  SigilRecord records_[MAX_PHYSICAL_SIGILS];
  QueueHandle_t eventQueue_ = nullptr;
  QueueHandle_t txQueue_ = nullptr;
  TaskHandle_t txTask_ = nullptr;
};

}  // namespace TurnHub
