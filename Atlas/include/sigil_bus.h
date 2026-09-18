#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "protocol.h"
#include "turnhub_types.h"

namespace TurnHub {

struct SigilEvent {
  uint8_t sigilId = INVALID_ID;
  TurnHubProtocol::PacketType type = TurnHubProtocol::PacketType::Hello;
  int32_t value = 0;
};

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
};

class SigilBus {
 public:
  explicit SigilBus(uint8_t wifiChannel);

  bool begin();
  bool poll(SigilEvent &event);

  uint8_t activeCount(uint32_t nowMs) const;
  bool isOnline(uint8_t sigilId, uint32_t nowMs) const;
  const SigilRecord *record(uint8_t sigilId) const;

  bool setBlue(uint8_t sigilId, uint8_t brightness);
  bool setRed(uint8_t sigilId, bool on);
  bool setGreen(uint8_t sigilId, bool on);
  bool buzzer(uint8_t sigilId, int32_t value);

  bool send(
      uint8_t sigilId,
      TurnHubProtocol::PacketType type,
      int32_t value = 0);

  // Web controls and future local integrations feed the same event queue as
  // physical Sigils, keeping game behavior centralized in main.cpp.
  bool injectEvent(
      uint8_t sigilId,
      TurnHubProtocol::PacketType type,
      int32_t value = 0);

  static SigilBus *activeInstance();
  static constexpr uint32_t SIGIL_TIMEOUT_MS = 7000;

 private:
  static SigilBus *instance_;
  static void receiveThunk(
      const uint8_t *mac,
      const uint8_t *incomingData,
      int length);

  void handleReceive(
      const uint8_t *mac,
      const uint8_t *incomingData,
      int length);

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
  static void printMac(const uint8_t *mac);

  uint8_t wifiChannel_;
  SigilRecord records_[MAX_PHYSICAL_SIGILS];
  QueueHandle_t eventQueue_ = nullptr;
};

}  // namespace TurnHub
