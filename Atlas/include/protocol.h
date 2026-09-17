#pragma once

#include <Arduino.h>

namespace TurnHubProtocol {

constexpr uint8_t VERSION = 1;
constexpr uint8_t MAX_SIGILS = 8;

// Shared ESP-NOW message types. Keep the values stable once devices begin
// shipping so newer Atlas firmware can identify older Sigil packets.
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

// Buzzer packets keep the compact 7-byte packet layout by packing one tone
// into the existing 32-bit value: frequency in the upper 16 bits and duration
// in milliseconds in the lower 16 bits. A zero frequency or duration means
// stop/silence.
inline int32_t encodeTone(uint16_t frequencyHz, uint16_t durationMs) {
  return static_cast<int32_t>(
      (static_cast<uint32_t>(frequencyHz) << 16) |
      static_cast<uint32_t>(durationMs));
}

inline uint16_t toneFrequency(int32_t value) {
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(value) >> 16) & 0xFFFFu);
}

inline uint16_t toneDuration(int32_t value) {
  return static_cast<uint16_t>(
      static_cast<uint32_t>(value) & 0xFFFFu);
}

}  // namespace TurnHubProtocol
