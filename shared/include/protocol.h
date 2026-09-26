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
// FactoryReset payload: a fixed value ("FRES"), so no stray or corrupted
// packet can wipe a Sigil.
constexpr int32_t FACTORY_RESET_CONFIRM = 0x46524553;
// A queued PASS commits after this grace period unless the same seat cancels
// it. Atlas owns the timer; a Sigil uses it only to draw the countdown.
constexpr uint32_t PASS_GRACE_MS = 3000;

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
// The Sigil has five-key input (Up/Down/Left/Right/Select: joystick or d-pad)
// and shows Atlas's action menu. Atlas then sends MenuState, and the Sigil
// answers with SelectAction instead of the Action/Pass gestures.
constexpr uint8_t CAPABILITY_MENU = 0x40;
// A hardware test harness (TestHarness/), not a player controller. Atlas then
// offers its premade tests on the touchscreen and sends HarnessCommand; the
// harness answers with HarnessReport. It still plays only through the normal
// Sigil packets, so it gains no authority.
constexpr uint8_t CAPABILITY_HARNESS = 0x80;

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
  // Sigil -> Atlas: the player chose a menu action (encodeSelectAction).
  SelectAction = 13,
  // Harness -> Atlas: test run progress (encodeHarnessReport).
  HarnessReport = 14,
  // Sigil -> Atlas: a key pressed in the profile picker (encodePickerKey).
  PickerKey = 15,
  // Sigil -> Atlas (0.8.0+): change one of this Sigil's players' life by a
  // batched amount (encodeLifeAdjust). Offered while AdjustLife is in the menu.
  LifeAdjust = 16,
  // Sigil -> Atlas (0.8.0+): approve or deny the life request shown
  // (encodeLifeResponse).
  LifeResponse = 17,
  SetBlue = 20,
  SetRed = 21,
  SetGreen = 22,
  Buzzer = 23,
  InputTiming = 24,  // Atlas -> Sigil: hold thresholds (encodeInputTiming).
  LedState = 25,     // Atlas -> Sigil: semantic light state (encodeLedState).
  MenuState = 26,    // Atlas -> Sigil: actions available now (encodeMenuState).
  HarnessCommand = 27,  // Atlas -> harness: run or stop a test (encodeHarnessCommand).
  // Atlas -> Sigil: erase all saved settings (NVS) and restart. Only honored
  // from the paired Atlas, for this Sigil's ID, with FACTORY_RESET_CONFIRM.
  FactoryReset = 28,
  // Atlas -> Sigil: the profile picker page (ProfilePickerPacket, by length).
  ProfilePicker = 33,
  // Atlas -> Sigil 0.8.0+: actions available now, with room for actions past
  // the first 21 (encodeMenuState2). Older Sigils keep MenuState.
  MenuState2 = 34,
  // Atlas -> Sigil 0.8.0+: a pending life request for one of this Sigil's
  // players, or none (encodeLifeRequest). Resent with every Hello.
  LifeRequest = 35,
  // Atlas -> Sigil: a seated profile's chosen Jewel color for one seat, or
  // none (encodeSeatColor). Resent with every Hello; older Sigils ignore it.
  SeatColor = 36,
  // Atlas -> Sigil: the running game's starting life (0 = no game), so the
  // Sigil's heart can shrink or grow against it. Resent with every Hello;
  // older Sigils ignore it. Presentation only.
  StartingLife = 37,
  // Atlas -> Sigil: the player number whose pass is in its grace period
  // (0 = none), sent to every Sigil so the whole table sees it. Resent with
  // every Hello; older Sigils ignore it. Presentation only.
  PassPending = 38,
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

// Profile picker (menu Sigils 0.8.0+: sent only to Sigils that advertise
// CAPABILITY_MENU without CAPABILITY_HARNESS and report at least
// PICKER_MIN_FIRMWARE). Atlas owns the list, the page and every rule; the
// Sigil draws the page and reports compass keys. Keys map to fixed places so
// an e-ink panel redraws once per page, not per cursor move (the OLED shows a
// list and turns the chosen row into the same key):
//   Up, Right, Down: the three names on the page     Left: back / cancel
//   Select (click): more names (List) or yes (Confirm)
constexpr uint8_t PICKER_MIN_FIRMWARE_MAJOR = 0;
constexpr uint8_t PICKER_MIN_FIRMWARE_MINOR = 8;
constexpr uint8_t PICKER_PAGE_ITEMS = 3;

enum class PickerMode : uint8_t {
  Closed = 0,   // Back to the action menu.
  List = 1,     // Choose a name.
  Confirm = 2,  // "Join as <name>?" (item 0 holds the name).
};
// Why the last choice did not go through, shown as text on the page.
enum class PickerNotice : uint8_t {
  None = 0,
  NeedsPhone = 1,   // Physical use needs this profile signed in on a phone.
  Unavailable = 2,  // Already playing on another Sigil, left, or blocked.
  TableFull = 3,
  Failed = 4,
  Count
};
constexpr uint8_t PICKER_ITEM_GUEST = 0x01;   // The guest entry, not a profile.
constexpr uint8_t PICKER_ITEM_LOCKED = 0x02;  // Needs phone sign-in first.
constexpr uint8_t PICKER_ITEM_PLAYING = 0x04; // At the table already (attach).

struct __attribute__((packed)) PickerItem {
  uint8_t flags;
  char name[DISPLAY_NAME_MAX_LENGTH + 1];
};
struct __attribute__((packed)) ProfilePickerPacket {
  uint8_t version;
  PacketType type;
  uint8_t sigilId;
  uint8_t revision;   // A PickerKey names the page it was pressed on.
  PickerMode mode;
  PickerNotice notice;
  uint8_t page;       // 0-based.
  uint8_t pageCount;
  uint8_t itemCount;  // 0..PICKER_PAGE_ITEMS
  PickerItem items[PICKER_PAGE_ITEMS];
};
static_assert(sizeof(ProfilePickerPacket) == 51, "Profile picker wire layout changed");
inline bool validProfilePicker(const ProfilePickerPacket &p) {
  if (p.version != VERSION || p.type != PacketType::ProfilePicker || p.sigilId >= MAX_SIGILS ||
      static_cast<uint8_t>(p.mode) > static_cast<uint8_t>(PickerMode::Confirm) ||
      static_cast<uint8_t>(p.notice) >= static_cast<uint8_t>(PickerNotice::Count) ||
      p.itemCount > PICKER_PAGE_ITEMS || p.pageCount == 0 || p.page >= p.pageCount ||
      (p.mode == PickerMode::Confirm && p.itemCount != 1)) return false;
  for (uint8_t i = 0; i < PICKER_PAGE_ITEMS; ++i) {
    if (p.items[i].name[DISPLAY_NAME_MAX_LENGTH]) return false;
  }
  return true;
}
inline bool pickerFirmware(uint8_t major, uint8_t minor) {
  return major > PICKER_MIN_FIRMWARE_MAJOR ||
      (major == PICKER_MIN_FIRMWARE_MAJOR && minor >= PICKER_MIN_FIRMWARE_MINOR);
}
// PickerKey payload: bits 0-2 key (0 Up, 1 Down, 2 Left, 3 Right, 4 Select),
// bits 3-10 the page revision it was pressed on.
enum class PickerKeyCode : uint8_t { Up = 0, Down = 1, Left = 2, Right = 3, Select = 4, Count };
inline int32_t encodePickerKey(PickerKeyCode key, uint8_t revision) {
  return static_cast<int32_t>((static_cast<uint32_t>(key) & 0x07u) |
      (static_cast<uint32_t>(revision) << 3));
}
inline uint8_t pickerKeyCode(int32_t value) {
  return static_cast<uint8_t>(static_cast<uint32_t>(value) & 0x07u);
}
inline uint8_t pickerKeyRevision(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 3) & 0xFFu);
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

// Actions a menu Sigil can offer. Atlas decides which are available for each
// Sigil (MenuState) and validates every choice through its Intent handlers;
// the Sigil only lists them. Wire values are stable; at most 21 (mask bits).
enum class SigilAction : uint8_t {
  Join = 0,
  CycleStarter = 1,
  RandomStarter = 2,
  AddSeatB = 3,
  RemoveSeatB = 4,
  StartGame = 5,
  CancelStart = 6,
  Pass = 7,
  CancelPass = 8,
  Pause = 9,
  Resume = 10,
  ClaimWin = 11,
  ConfirmWin = 12,
  DenyWin = 13,
  BeginElimination = 14,  // "I'm out": start choosing this Sigil's seat to eliminate.
  NextTarget = 15,        // Switch the elimination to this Sigil's other seat.
  Eliminate = 16,
  CancelElimination = 17,
  Rematch = 18,
  ResetTable = 19,
  LinkPhone = 20,         // Approve a waiting browser link for this Sigil.
  // MenuState2 only (Sigil 0.8.0+): this Sigil leaves the lobby, both seats.
  Leave = 21,
  // MenuState2 only: Left/Right (when no other action has them) change this
  // Sigil's shown player's life; sent as LifeAdjust, never SelectAction.
  AdjustLife = 22,
  Count
};
constexpr uint8_t SIGIL_ACTION_NONE = 31;
// MenuState carries actions 0-20; MenuState2 carries up to 24.
constexpr uint32_t SIGIL_ACTION_MASK_BITS = 21;
constexpr uint32_t SIGIL_ACTION_MASK2_BITS = 24;
static_assert(static_cast<uint8_t>(SigilAction::Count) <= SIGIL_ACTION_MASK2_BITS,
    "SigilAction must fit the MenuState2 mask");
// Menu revisions wrap at 8 so both encodings can name them.
constexpr uint8_t MENU_REVISION_MASK = 0x07;

constexpr uint32_t sigilActionBit(SigilAction action) {
  return 1u << static_cast<uint8_t>(action);
}

// Deliberate actions are sent only after the key is held: the long-press or
// win-hold threshold from the seated players' InputTiming.
enum class ActionHold : uint8_t { None, Long, Win };
inline ActionHold sigilActionHold(SigilAction action) {
  switch (action) {
    case SigilAction::ClaimWin: return ActionHold::Win;
    case SigilAction::Eliminate:
    case SigilAction::ResetTable:
    case SigilAction::Leave: return ActionHold::Long;
    default: return ActionHold::None;
  }
}

// MenuState payload: bits 0-20 available actions, 21-25 the default action
// (SIGIL_ACTION_NONE if none), 26-31 menu revision (wraps). A SelectAction
// names the revision it was chosen from, so Atlas ignores stale choices.
struct MenuStateFields {
  uint32_t actions = 0;
  uint8_t defaultAction = SIGIL_ACTION_NONE;
  uint8_t revision = 0;
};

inline int32_t encodeMenuState(const MenuStateFields &f) {
  return static_cast<int32_t>(
      (f.actions & ((1u << SIGIL_ACTION_MASK_BITS) - 1)) |
      ((static_cast<uint32_t>(f.defaultAction) & 0x1Fu) << 21) |
      ((static_cast<uint32_t>(f.revision) & 0x3Fu) << 26));
}

inline MenuStateFields decodeMenuState(int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  MenuStateFields f;
  f.actions = v & ((1u << SIGIL_ACTION_MASK_BITS) - 1) &
      ((1u << static_cast<uint8_t>(SigilAction::Count)) - 1);
  f.defaultAction = static_cast<uint8_t>((v >> 21) & 0x1Fu);
  if (f.defaultAction >= static_cast<uint8_t>(SigilAction::Count) ||
      (f.actions & (1u << f.defaultAction)) == 0) {
    f.defaultAction = SIGIL_ACTION_NONE;
  }
  f.revision = static_cast<uint8_t>((v >> 26) & 0x3Fu);
  return f;
}

// MenuState2 payload (Sigil 0.8.0+): bits 0-23 available actions, 24-28 the
// default action (SIGIL_ACTION_NONE if none), 29-31 menu revision.
inline int32_t encodeMenuState2(const MenuStateFields &f) {
  return static_cast<int32_t>(
      (f.actions & ((1u << SIGIL_ACTION_MASK2_BITS) - 1)) |
      ((static_cast<uint32_t>(f.defaultAction) & 0x1Fu) << 24) |
      ((static_cast<uint32_t>(f.revision) & MENU_REVISION_MASK) << 29));
}

inline MenuStateFields decodeMenuState2(int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  MenuStateFields f;
  f.actions = v & ((1u << SIGIL_ACTION_MASK2_BITS) - 1) &
      ((1u << static_cast<uint8_t>(SigilAction::Count)) - 1);
  f.defaultAction = static_cast<uint8_t>((v >> 24) & 0x1Fu);
  if (f.defaultAction >= static_cast<uint8_t>(SigilAction::Count) ||
      (f.actions & (1u << f.defaultAction)) == 0) {
    f.defaultAction = SIGIL_ACTION_NONE;
  }
  f.revision = static_cast<uint8_t>((v >> 29) & MENU_REVISION_MASK);
  return f;
}

// Life on a menu Sigil. Presses are batched: the Sigil sends one LifeAdjust
// LIFE_ADJUST_COMMIT_MS after the last change. Holding a key repeats, then
// switches to LIFE_ADJUST_FAST_STEP steps for big jumps.
constexpr uint32_t LIFE_ADJUST_COMMIT_MS = 2000;
constexpr uint32_t LIFE_ADJUST_REPEAT_DELAY_MS = 500;
constexpr uint32_t LIFE_ADJUST_REPEAT_MS = 150;
constexpr uint32_t LIFE_ADJUST_FAST_AFTER_MS = 1500;
constexpr int32_t LIFE_ADJUST_FAST_STEP = 5;
constexpr int32_t LIFE_ADJUST_MAX = 9999;

// LifeAdjust payload: bits 0-4 player number, bits 5-31 signed delta.
inline int32_t encodeLifeAdjust(uint8_t player, int32_t delta) {
  return static_cast<int32_t>((static_cast<uint32_t>(player) & 0x1Fu) |
      (static_cast<uint32_t>(delta) << 5));
}
inline uint8_t lifeAdjustPlayer(int32_t value) {
  return static_cast<uint8_t>(static_cast<uint32_t>(value) & 0x1Fu);
}
inline int32_t lifeAdjustDelta(int32_t value) { return value >> 5; }

// LifeRequest payload: bits 0-4 target player (0 = none), 5-9 requesting
// player, 10-15 request tag (request id & 63), 16-31 signed delta (clamped).
struct LifeRequestFields {
  uint8_t target = 0;
  uint8_t requester = 0;
  uint8_t tag = 0;
  int32_t delta = 0;
};
inline int32_t encodeLifeRequest(const LifeRequestFields &f) {
  int32_t d = f.delta;
  if (d > 32767) d = 32767;
  if (d < -32768) d = -32768;
  return static_cast<int32_t>((static_cast<uint32_t>(f.target) & 0x1Fu) |
      ((static_cast<uint32_t>(f.requester) & 0x1Fu) << 5) |
      ((static_cast<uint32_t>(f.tag) & 0x3Fu) << 10) |
      (static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(d))) << 16));
}
inline LifeRequestFields decodeLifeRequest(int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  LifeRequestFields f;
  f.target = static_cast<uint8_t>(v & 0x1Fu);
  f.requester = static_cast<uint8_t>((v >> 5) & 0x1Fu);
  f.tag = static_cast<uint8_t>((v >> 10) & 0x3Fu);
  f.delta = static_cast<int16_t>(static_cast<uint16_t>(v >> 16));
  return f;
}
// LifeResponse payload: bits 0-4 target player, bit 5 approve, 6-11 tag.
inline int32_t encodeLifeResponse(uint8_t target, bool approve, uint8_t tag) {
  return static_cast<int32_t>((static_cast<uint32_t>(target) & 0x1Fu) |
      (approve ? 0x20u : 0u) | ((static_cast<uint32_t>(tag) & 0x3Fu) << 6));
}
inline uint8_t lifeResponseTarget(int32_t v) { return static_cast<uint8_t>(static_cast<uint32_t>(v) & 0x1Fu); }
inline bool lifeResponseApprove(int32_t v) { return (static_cast<uint32_t>(v) & 0x20u) != 0; }
inline uint8_t lifeResponseTag(int32_t v) { return static_cast<uint8_t>((static_cast<uint32_t>(v) >> 6) & 0x3Fu); }

// SeatColor payload: bits 0-1 seat (1 = A, 2 = B), bit 2 color set (else
// the default look), bits 3-7 the seat's preset avatar (avatars.h; 0 none,
// never a custom one), bits 8-31 0xRRGGBB. The Sigil uses the color only for
// the calm Joined and Waiting cues; every action cue keeps its standard color.
inline int32_t encodeSeatColor(uint8_t slot, bool set, uint32_t rgb, uint8_t avatar = 0) {
  return static_cast<int32_t>((static_cast<uint32_t>(slot) & 0x03u) | (set ? 0x04u : 0u) |
      ((static_cast<uint32_t>(avatar) & 0x1Fu) << 3) | ((rgb & 0xFFFFFFu) << 8));
}
inline uint8_t seatAvatar(int32_t v) { return static_cast<uint8_t>((static_cast<uint32_t>(v) >> 3) & 0x1Fu); }
inline uint8_t seatColorSlot(int32_t v) { return static_cast<uint8_t>(static_cast<uint32_t>(v) & 0x03u); }
inline bool seatColorSet(int32_t v) { return (static_cast<uint32_t>(v) & 0x04u) != 0; }
inline uint32_t seatColorRgb(int32_t v) { return (static_cast<uint32_t>(v) >> 8) & 0xFFFFFFu; }

// Sigil firmware that decodes MenuState2 (the same release as the picker).
inline bool menuState2Firmware(uint8_t major, uint8_t minor) {
  return pickerFirmware(major, minor);
}

// SelectAction payload: bits 0-4 action, 5-10 menu revision.
inline int32_t encodeSelectAction(SigilAction action, uint8_t revision) {
  return static_cast<int32_t>((static_cast<uint32_t>(action) & 0x1Fu) |
      ((static_cast<uint32_t>(revision) & 0x3Fu) << 5));
}
inline uint8_t selectedAction(int32_t value) {
  return static_cast<uint8_t>(static_cast<uint32_t>(value) & 0x1Fu);
}
inline uint8_t selectedRevision(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 5) & 0x3Fu);
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

// --- Test harness (CAPABILITY_HARNESS) -------------------------------------
// Premade tests the Atlas touchscreen can start. The harness owns what each
// one does; Atlas only names them.
enum class HarnessTest : uint8_t {
  RadioCheck = 0,   // Every virtual Sigil paired, answering Hello, with a menu.
  QuickGame = 1,    // 2 players, 3 turns, reset.
  FullGame = 2,     // 4 players, 6 turns, pause, elimination, win, reset.
  RematchGame = 3,  // 3 players: a game, rematch, a second game, reset.
  Soak = 4,         // Five 4-player games in a row.
  Count
};

inline const char *harnessTestName(uint8_t test) {
  switch (static_cast<HarnessTest>(test)) {
    case HarnessTest::RadioCheck: return "Radio check";
    case HarnessTest::QuickGame: return "2-player game";
    case HarnessTest::FullGame: return "4-player game";
    case HarnessTest::RematchGame: return "Rematch game";
    case HarnessTest::Soak: return "Soak x5";
    default: return "Unknown test";
  }
}

// The checkpoints a run reports, in the order a game reaches them.
enum class HarnessStep : uint8_t {
  None = 0, Paired, HelloAck, Menu, Lobby, Join, SeatB, Host, Start, Turn, Pause, Resume,
  Eliminate, ResumeAfterElimination, ClaimWin, ConfirmWin, GameOver, RematchLobby, ResetTable,
  Count
};

inline const char *harnessStepName(uint8_t step) {
  static const char *const NAMES[] = {"NONE", "PAIRED", "HELLO_ACK", "MENU", "LOBBY", "JOIN",
      "SEAT_B", "HOST", "START", "TURN", "PAUSE", "RESUME", "ELIMINATE",
      "RESUME_AFTER_ELIMINATION", "CLAIM_WIN", "CONFIRM_WIN", "GAME_OVER", "REMATCH_LOBBY",
      "RESET_TABLE"};
  static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == static_cast<size_t>(HarnessStep::Count),
      "One name per HarnessStep");
  return step < static_cast<uint8_t>(HarnessStep::Count) ? NAMES[step] : "UNKNOWN";
}

enum class HarnessCommandKind : uint8_t { Run = 0, Stop = 1 };

// HarnessCommand payload: bits 0-3 kind, 4-7 test.
inline int32_t encodeHarnessCommand(HarnessCommandKind kind, HarnessTest test = HarnessTest::RadioCheck) {
  return static_cast<int32_t>((static_cast<uint32_t>(kind) & 0x0Fu) |
      ((static_cast<uint32_t>(test) & 0x0Fu) << 4));
}
inline uint8_t harnessCommandKind(int32_t value) {
  return static_cast<uint8_t>(static_cast<uint32_t>(value) & 0x0Fu);
}
inline uint8_t harnessCommandTest(int32_t value) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) >> 4) & 0x0Fu);
}

enum class HarnessRunState : uint8_t { Idle = 0, Running = 1, Passed = 2, Failed = 3, Stopped = 4 };

// HarnessReport payload: bits 0-2 state, 3-7 test, 8-15 last step, 16-23
// steps passed, 24-31 steps failed (both saturate at 255).
struct HarnessReportFields {
  HarnessRunState state = HarnessRunState::Idle;
  uint8_t test = 0;
  uint8_t step = 0;
  uint8_t passed = 0;
  uint8_t failed = 0;
};

inline int32_t encodeHarnessReport(const HarnessReportFields &f) {
  return static_cast<int32_t>((static_cast<uint32_t>(f.state) & 0x07u) |
      ((static_cast<uint32_t>(f.test) & 0x1Fu) << 3) |
      (static_cast<uint32_t>(f.step) << 8) |
      (static_cast<uint32_t>(f.passed) << 16) |
      (static_cast<uint32_t>(f.failed) << 24));
}

inline HarnessReportFields decodeHarnessReport(int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  HarnessReportFields f;
  const uint8_t state = static_cast<uint8_t>(v & 0x07u);
  f.state = state <= static_cast<uint8_t>(HarnessRunState::Stopped)
      ? static_cast<HarnessRunState>(state) : HarnessRunState::Idle;
  f.test = static_cast<uint8_t>((v >> 3) & 0x1Fu);
  f.step = static_cast<uint8_t>((v >> 8) & 0xFFu);
  f.passed = static_cast<uint8_t>((v >> 16) & 0xFFu);
  f.failed = static_cast<uint8_t>((v >> 24) & 0xFFu);
  return f;
}

}  // namespace TurnHubProtocol
