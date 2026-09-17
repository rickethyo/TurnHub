#pragma once

#include <Arduino.h>

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
  DisplayState = 30,
};

enum class DisplayMode : uint8_t {
  Ready = 1,
  Joined = 2,
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

inline int32_t encodeDisplayState(
    DisplayMode mode,
    uint8_t primaryPlayer,
    uint8_t secondaryPlayer,
    uint8_t turnNumber) {
  return static_cast<int32_t>(
      static_cast<uint32_t>(mode) |
      (static_cast<uint32_t>(primaryPlayer) << 8) |
      (static_cast<uint32_t>(secondaryPlayer) << 16) |
      (static_cast<uint32_t>(turnNumber) << 24));
}

inline DisplayMode displayMode(int32_t value) {
  return static_cast<DisplayMode>(static_cast<uint32_t>(value) & 0xFFu);
}

inline uint8_t displayPrimaryPlayer(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 8) & 0xFFu);
}

inline uint8_t displaySecondaryPlayer(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 16) & 0xFFu);
}

inline uint8_t displayTurnNumber(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 24) & 0xFFu);
}

}  // namespace TurnHubProtocol
