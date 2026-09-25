#pragma once

#include <Arduino.h>

namespace TurnHubProtocol {

constexpr uint8_t VERSION = 1;
constexpr uint8_t MAX_SIGILS = 8;
constexpr uint8_t DISPLAY_NAME_MAX_LENGTH = 12;
constexpr uint8_t DISPLAY_NAME_CHUNK_CHARS = 3;

// Deliberate manual-pairing window. It is the Sigil's window and Atlas's
// default; an Atlas admin may lengthen Atlas's own window (pairing_settings.h).
constexpr uint32_t PAIRING_WINDOW_MS = 15000;
// Holding a Sigil's Pair button this long erases its saved Atlas pairing.
constexpr uint32_t FORGET_PAIRING_HOLD_MS = 10000;

constexpr uint8_t CAPABILITY_DISPLAY = 0x01;
constexpr uint8_t CAPABILITY_DISPLAY_PROFILE = 0x02;
constexpr uint8_t CAPABILITY_GAME_DISPLAY = 0x04;
// The Sigil applies InputTiming packets (adjustable hold thresholds).
constexpr uint8_t CAPABILITY_INPUT_TIMING = 0x08;
// The Sigil has the 1.3" OLED display (the sigil-oled build). Atlas seats one
// player on it: shared seating (Seat B) is e-paper only. E-paper Sigils, and
// firmware from before this bit existed, leave it clear.
constexpr uint8_t CAPABILITY_DISPLAY_OLED = 0x10;
// The Sigil renders its status light itself from LedState packets (full
// color, and the NeoPixel ring's pixels). Atlas then sends LedState instead
// of the SetBlue/SetRed/SetGreen channel stream; older Sigils keep that.
constexpr uint8_t CAPABILITY_LED_STATE = 0x20;

// Action-button hold thresholds. Atlas chooses them from the seated players'
// accessibility preferences and sends them in InputTiming; the Sigil applies
// them at runtime only and uses the defaults until Atlas says otherwise.
// Changing a threshold never changes what the resulting Intent means.
constexpr uint16_t DEFAULT_LONG_PRESS_MS = 2000;
constexpr uint16_t DEFAULT_WIN_HOLD_MS = 5000;
constexpr uint16_t MIN_LONG_PRESS_MS = 1000;
constexpr uint16_t MAX_LONG_PRESS_MS = 4000;
constexpr uint16_t MIN_WIN_HOLD_MS = 3000;
constexpr uint16_t MAX_WIN_HOLD_MS = 10000;
// A win hold always outlasts the long press by at least this much, so the
// pause gesture stays reachable without claiming a win.
constexpr uint16_t MIN_HOLD_GAP_MS = 1000;
constexpr uint16_t HOLD_STEP_MS = 250;

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
  DisplayProfileRequest = 9,
  PairRequest = 10,
  PairAccept = 11,
  // Atlas -> Sigil: Atlas forgot this Sigil; it erases its saved pairing.
  // Best effort: a Sigil that misses it stays paired until re-paired or reset.
  Unpair = 12,
  SetBlue = 20,
  SetRed = 21,
  SetGreen = 22,
  Buzzer = 23,
  InputTiming = 24,  // Atlas -> Sigil: hold thresholds (encodeInputTiming).
  LedState = 25,     // Atlas -> Sigil: semantic light state (encodeLedState).
  DisplayState = 30,
  DisplayNameChunk = 31,
  GameDisplay = 32,
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

// Atomic rendering snapshot, little-endian like Packet. Never authoritative.
constexpr uint8_t DISPLAY_COMMANDER_SOURCES = 3;
struct __attribute__((packed)) DisplayParticipant {
  int32_t life;
  char name[DISPLAY_NAME_MAX_LENGTH + 1];
};
struct __attribute__((packed)) DisplayCommanderSource {
  uint8_t player;
  char name[DISPLAY_NAME_MAX_LENGTH + 1];
  int32_t damage[2];
};
struct __attribute__((packed)) GameDisplayPacket {
  uint8_t version;
  PacketType type;
  uint8_t sigilId;
  int32_t state;
  uint8_t commander;
  uint8_t sourceCount;
  uint8_t omittedSources;
  DisplayParticipant primary;
  DisplayParticipant secondary;
  DisplayCommanderSource sources[DISPLAY_COMMANDER_SOURCES];
};
static_assert(sizeof(GameDisplayPacket) == 110, "Game display wire layout changed");
inline bool validGameDisplay(const GameDisplayPacket &p) {
  if (p.version != VERSION || p.type != PacketType::GameDisplay ||
      p.sigilId >= MAX_SIGILS || p.commander > 1 ||
      (static_cast<uint32_t>(p.state) & DISPLAY_MODE_MASK) != static_cast<uint8_t>(DisplayMode::Running) ||
      p.primary.life < -1000000 || p.primary.life > 1000000 ||
      p.secondary.life < -1000000 || p.secondary.life > 1000000 ||
      (!p.commander && (p.sourceCount || p.omittedSources)) ||
      p.sourceCount > DISPLAY_COMMANDER_SOURCES || p.omittedSources > 15 ||
      p.primary.name[DISPLAY_NAME_MAX_LENGTH] || p.secondary.name[DISPLAY_NAME_MAX_LENGTH]) return false;
  for (uint8_t i = 0; i < p.sourceCount; ++i) {
    if (!p.sources[i].player || p.sources[i].player > 16 ||
        p.sources[i].name[DISPLAY_NAME_MAX_LENGTH] ||
        p.sources[i].damage[0] < 0 || p.sources[i].damage[1] < 0 ||
        p.sources[i].damage[0] > 1000000 || p.sources[i].damage[1] > 1000000) return false;
  }
  return true;
}

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

// Player names are sent separately from fast-changing DisplayState packets.
// Each packet carries three printable name bytes. Header layout:
// bits 0..1 = seat (1=A, 2=B), bits 2..5 = chunk index, bit 7 = final chunk.
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

// InputTiming value: long-press ms in bits 0..15, win-hold ms in bits 16..31.
// Status-light vocabulary. Atlas chooses the cue and overlays from game state
// (it owns the meaning); the Sigil only renders them. Wire values are stable.
// Every cue stays distinguishable by pattern or position, not only by hue
// (ACCESSIBILITY.md), and essential meaning also reaches screens as text.
enum class LedCue : uint8_t {
  Off = 0,             // Not part of the current table or game.
  Unassigned = 1,      // Online but not joined: lobby invitation.
  Joined = 2,          // In the lobby; shows the player number.
  Starting = 3,        // Start countdown.
  TurnStarted = 4,     // First moments of a turn that just passed to this Sigil.
  YourTurn = 5,
  Waiting = 6,         // Another player's turn.
  Paused = 7,
  ConfirmationNeeded = 8,  // This Sigil must confirm or deny a win claim.
  EliminationSelect = 9,   // This Sigil is choosing a player to eliminate.
  GameOver = 10,
  // Sigil-local: Atlas cannot drive a Sigil that is pairing or offline.
  Pairing = 11,
  Disconnected = 12,
  Error = 13,
  Count
};

// Facets layered over the primary cue.
enum class LedOverlay : uint8_t {
  Host = 0,
  Starter = 1,
  Winner = 2,
  TurnWarning = 3,   // Turn timer: little time left.
  TimerExpired = 4,  // Turn timer reached zero; the turn continues.
  LongTurn = 5,      // Timer off and the turn passed the long-turn mark.
  Count
};

// The seated players' LED accessibility choice (profile LedStyle).
enum class LedStyle : uint8_t { Default = 0, ReducedMotion = 1, MonochromeSafe = 2 };

// LedState payload:
//   bits 0-3 cue, 4-9 overlay bits, 10-13 player number (0-15),
//   14 seat B (else A), 15 shared seat, 16-17 style,
//   18-31 time since the cue's anchor (turn or countdown start) in 16 ms
//   units, clamped (~262 s); only anchored patterns use it.
constexpr uint32_t LED_ANCHOR_UNIT_MS = 16;
constexpr uint32_t LED_ANCHOR_MAX_UNITS = 0x3FFF;

struct LedStateFields {
  LedCue cue = LedCue::Off;
  uint8_t overlays = 0;
  uint8_t playerNumber = 0;
  uint8_t seatSlot = 1;  // 1 = A, 2 = B.
  bool sharedSeat = false;
  LedStyle style = LedStyle::Default;
  uint32_t anchorAgeMs = 0;
};

inline int32_t encodeLedState(const LedStateFields &f) {
  uint32_t units = f.anchorAgeMs / LED_ANCHOR_UNIT_MS;
  if (units > LED_ANCHOR_MAX_UNITS) units = LED_ANCHOR_MAX_UNITS;
  return static_cast<int32_t>(
      (static_cast<uint32_t>(f.cue) & 0x0Fu) |
      ((static_cast<uint32_t>(f.overlays) & 0x3Fu) << 4) |
      ((static_cast<uint32_t>(f.playerNumber) & 0x0Fu) << 10) |
      (f.seatSlot == 2 ? 1u << 14 : 0u) |
      (f.sharedSeat ? 1u << 15 : 0u) |
      ((static_cast<uint32_t>(f.style) & 0x03u) << 16) |
      (units << 18));
}

// Everything except the anchor age: what makes a new LedState worth sending.
inline uint32_t ledStateKey(int32_t value) {
  return static_cast<uint32_t>(value) & 0x3FFFFu;
}

inline LedStateFields decodeLedState(int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  LedStateFields f;
  const uint8_t cue = static_cast<uint8_t>(v & 0x0Fu);
  f.cue = cue < static_cast<uint8_t>(LedCue::Count) ? static_cast<LedCue>(cue) : LedCue::Off;
  f.overlays = static_cast<uint8_t>((v >> 4) & 0x3Fu);
  f.playerNumber = static_cast<uint8_t>((v >> 10) & 0x0Fu);
  f.seatSlot = (v & (1u << 14)) ? 2 : 1;
  f.sharedSeat = (v & (1u << 15)) != 0;
  const uint8_t style = static_cast<uint8_t>((v >> 16) & 0x03u);
  f.style = style <= static_cast<uint8_t>(LedStyle::MonochromeSafe)
      ? static_cast<LedStyle>(style) : LedStyle::Default;
  f.anchorAgeMs = (v >> 18) * LED_ANCHOR_UNIT_MS;
  return f;
}

inline bool validInputTiming(uint16_t longPressMs, uint16_t winHoldMs) {
  return longPressMs >= MIN_LONG_PRESS_MS && longPressMs <= MAX_LONG_PRESS_MS &&
      winHoldMs >= MIN_WIN_HOLD_MS && winHoldMs <= MAX_WIN_HOLD_MS &&
      longPressMs % HOLD_STEP_MS == 0 && winHoldMs % HOLD_STEP_MS == 0 &&
      static_cast<uint32_t>(winHoldMs) >= static_cast<uint32_t>(longPressMs) + MIN_HOLD_GAP_MS;
}
inline int32_t encodeInputTiming(uint16_t longPressMs, uint16_t winHoldMs) {
  return static_cast<int32_t>(
      static_cast<uint32_t>(longPressMs) | (static_cast<uint32_t>(winHoldMs) << 16));
}
inline uint16_t inputTimingLongPress(int32_t value) {
  return static_cast<uint16_t>(static_cast<uint32_t>(value) & 0xFFFFu);
}
inline uint16_t inputTimingWinHold(int32_t value) {
  return static_cast<uint16_t>((static_cast<uint32_t>(value) >> 16) & 0xFFFFu);
}

}  // namespace TurnHubProtocol
