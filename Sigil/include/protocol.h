#pragma once

#include <Arduino.h>

namespace TurnHubProtocol {

constexpr uint8_t VERSION = 1;
constexpr uint8_t MAX_SIGILS = 8;
constexpr uint8_t DISPLAY_NAME_MAX_LENGTH = 12;
constexpr uint8_t DISPLAY_NAME_CHUNK_CHARS = 3;

enum class PacketType : uint8_t {
  Hello = 1,
  Ack = 2,
  Pass = 3,
  ActionDown = 4,
  ActionUp = 5,
  ActionShort = 6,
  ActionLong = 7,
  ActionWin = 8,
  DisplayProfileRequest = 9,
  SetBlue = 20,
  SetRed = 21,
  SetGreen = 22,
  Buzzer = 23,
  DisplayState = 30,
  DisplayNameChunk = 31,
};

enum class DisplayMode : uint8_t {
  Ready = 1,
  Lobby = 2,
  Starting = 3,
  Running = 4,
  Paused = 5,
  GameOver = 6,
};

constexpr uint8_t DISPLAY_MODE_MASK = 0x07;
constexpr uint8_t DISPLAY_FLAG_ACTIVE = 0x08;
constexpr uint8_t DISPLAY_FLAG_HOST = 0x10;
constexpr uint8_t DISPLAY_FLAG_STARTER = 0x20;
constexpr uint8_t DISPLAY_FLAG_WINNER = 0x40;
constexpr uint8_t DISPLAY_FLAG_ATTENTION = 0x80;

struct __attribute__((packed)) Packet {
  uint8_t version;
  PacketType type;
  uint8_t sigilId;
  int32_t value;
};

static_assert(sizeof(Packet) == 7, "TurnHub ESP-NOW packet layout changed");

inline Packet makePacket(PacketType type, uint8_t sigilId, int32_t value = 0) {
  return Packet{VERSION, type, sigilId, value};
}

inline int32_t encodeHelloInfo(
    uint8_t firmwareMajor,
    uint8_t firmwareMinor,
    uint8_t firmwarePatch,
    uint8_t capabilities = 0) {
  return static_cast<int32_t>(
      static_cast<uint32_t>(capabilities) |
      (static_cast<uint32_t>(firmwarePatch) << 8) |
      (static_cast<uint32_t>(firmwareMinor) << 16) |
      (static_cast<uint32_t>(firmwareMajor) << 24));
}
inline uint8_t helloCapabilities(int32_t value) {
  return static_cast<uint8_t>(static_cast<uint32_t>(value) & 0xFFu);
}
inline uint8_t helloFirmwarePatch(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 8) & 0xFFu);
}
inline uint8_t helloFirmwareMinor(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 16) & 0xFFu);
}
inline uint8_t helloFirmwareMajor(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 24) & 0xFFu);
}

inline int32_t encodeTone(uint16_t frequencyHz, uint16_t durationMs) {
  return static_cast<int32_t>(
      (static_cast<uint32_t>(frequencyHz) << 16) |
      static_cast<uint32_t>(durationMs));
}
inline uint16_t toneFrequency(int32_t value) {
  return static_cast<uint16_t>((static_cast<uint32_t>(value) >> 16) & 0xFFFFu);
}
inline uint16_t toneDuration(int32_t value) {
  return static_cast<uint16_t>(static_cast<uint32_t>(value) & 0xFFFFu);
}

inline int32_t encodeDisplayState(
    DisplayMode mode,
    uint8_t primaryPlayer,
    uint8_t secondaryPlayer,
    uint8_t turnNumber,
    uint8_t flags = 0) {
  const uint8_t header =
      (static_cast<uint8_t>(mode) & DISPLAY_MODE_MASK) |
      (flags & static_cast<uint8_t>(~DISPLAY_MODE_MASK));
  return static_cast<int32_t>(
      static_cast<uint32_t>(header) |
      (static_cast<uint32_t>(primaryPlayer) << 8) |
      (static_cast<uint32_t>(secondaryPlayer) << 16) |
      (static_cast<uint32_t>(turnNumber) << 24));
}
inline DisplayMode displayMode(int32_t value) {
  return static_cast<DisplayMode>(
      static_cast<uint8_t>(static_cast<uint32_t>(value) & 0xFFu) & DISPLAY_MODE_MASK);
}
inline uint8_t displayFlags(int32_t value) {
  return static_cast<uint8_t>(static_cast<uint32_t>(value) & 0xFFu) &
      static_cast<uint8_t>(~DISPLAY_MODE_MASK);
}
inline bool hasDisplayFlag(int32_t value, uint8_t flag) {
  return (displayFlags(value) & flag) != 0;
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

inline int32_t encodeDisplayNameChunk(
    uint8_t slot,
    uint8_t chunkIndex,
    bool finalChunk,
    char char0,
    char char1,
    char char2) {
  const uint8_t header =
      (slot & 0x03u) |
      static_cast<uint8_t>((chunkIndex & 0x0Fu) << 2) |
      (finalChunk ? 0x80u : 0u);
  return static_cast<int32_t>(
      static_cast<uint32_t>(header) |
      (static_cast<uint32_t>(static_cast<uint8_t>(char0)) << 8) |
      (static_cast<uint32_t>(static_cast<uint8_t>(char1)) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(char2)) << 24));
}
inline uint8_t displayNameSlot(int32_t value) {
  return static_cast<uint8_t>(static_cast<uint32_t>(value) & 0x03u);
}
inline uint8_t displayNameChunkIndex(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 2) & 0x0Fu);
}
inline bool displayNameFinalChunk(int32_t value) {
  return (static_cast<uint32_t>(value) & 0x80u) != 0;
}
inline char displayNameChar(int32_t value, uint8_t index) {
  if (index >= DISPLAY_NAME_CHUNK_CHARS) return '\0';
  return static_cast<char>((static_cast<uint32_t>(value) >> (8u * (index + 1u))) & 0xFFu);
}

}  // namespace TurnHubProtocol
