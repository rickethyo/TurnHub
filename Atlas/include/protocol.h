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
  DisplayState = 30,
};

enum class DisplayMode : uint8_t {
  Ready = 1,
  Lobby = 2,
  Starting = 3,
  Running = 4,
  Paused = 5,
  GameOver = 6,
};

// The display-state mode lives in the low three bits of byte 0. The remaining
// bits are flags that let Atlas describe the local Sigil's role without
// growing the compact 7-byte ESP-NOW packet.
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

inline Packet makePacket(
    PacketType type,
    uint8_t sigilId,
    int32_t value = 0) {
  return Packet{VERSION, type, sigilId, value};
}

// Hello packets reuse the existing 32-bit value field for device metadata.
// This keeps the wire format at 7 bytes and remains backward compatible with
// older Atlas builds that ignored Hello.value.
// byte 0 = capabilities/reserved flags
// byte 1 = firmware patch
// byte 2 = firmware minor
// byte 3 = firmware major
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

// Display state packets keep the same compact 7-byte wire format.
// byte 0 = DisplayMode in bits 0..2 plus DISPLAY_FLAG_* bits
// byte 1 = primary player number (0 when not joined)
// byte 2 = secondary player number (0 when unused)
// byte 3 = personal turn number / next turn ordinal hint
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
      static_cast<uint8_t>(static_cast<uint32_t>(value) & 0xFFu) &
      DISPLAY_MODE_MASK);
}

inline uint8_t displayFlags(int32_t value) {
  return static_cast<uint8_t>(
      static_cast<uint32_t>(value) & 0xFFu) &
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

}  // namespace TurnHubProtocol
