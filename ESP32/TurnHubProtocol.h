#pragma once

#include <Arduino.h>

// Transitional copy of Atlas/include/protocol.h for the Arduino IDE Sigil
// sketch. These definitions must remain byte-for-byte compatible until the
// Sigil firmware is moved into the same PlatformIO project structure.
namespace TurnHubProtocol {

constexpr uint8_t VERSION = 1;
constexpr uint8_t MAX_SIGILS = 8;

enum class PacketType : uint8_t {
  Hello = 1,
  Ack = 2,
  Pass = 3,
  ActionDown = 4,
  ActionUp = 5,
  ActionShort = 6,
  ActionLong = 7,
  ActionWin = 8,
  SetBlue = 20,
  SetRed = 21,
  SetGreen = 22,
  Buzzer = 23,
};

struct __attribute__((packed)) Packet {
  uint8_t version;
  PacketType type;
  uint8_t sigilId;
  int32_t value;
};

static_assert(sizeof(Packet) == 7, "TurnHub ESP-NOW packet layout changed");

inline Packet makePacket(
    PacketType type,
    uint8_t sigilId,
    int32_t value = 0) {
  return Packet{VERSION, type, sigilId, value};
}

}  // namespace TurnHubProtocol
