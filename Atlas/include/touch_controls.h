#pragma once

// Atlas touchscreen: what the built-in TFT shows and what its touch buttons
// do. Hardware-independent so host tests drive it: atlas_display.cpp feeds
// touch samples in and draws the AtlasScreen this module builds.
//
// The touch adapter only builds Intents with IntentOrigin::AtlasHardware (the
// screen is part of the Atlas and its only physical input); Atlas's handlers
// decide every outcome. Its non-Intent actions change no table state: the
// presence code screen (cancelling a code a phone asked for), moving between screens
// (status, info, QR codes, test harness, Table) and starting a harness test.

#include <Arduino.h>

namespace TurnHubAtlas {

// Landscape screen size in pixels.
constexpr int16_t ATLAS_SCREEN_WIDTH = 320;
constexpr int16_t ATLAS_SCREEN_HEIGHT = 240;

// A touch counts as released only after this long without contact, because
// resistive panels drop out briefly during a press.
constexpr uint32_t TOUCH_RELEASE_MS = 60;
// A press that started on a button stays on it while within this many pixels
// of its edge, so resistive jitter and a rolling fingertip do not cancel it.
constexpr int16_t TOUCH_SLOP_PX = 12;
// How long an action message stays on screen.
constexpr uint32_t TOUCH_NOTICE_MS = 4000;

// Screen regions (atlas_display.cpp draws them; buttons live in the rows).
constexpr int16_t SCREEN_HEADER_H = 26;
constexpr int16_t SCREEN_HERO_Y = 28;       // Title and detail/notice line.
constexpr int16_t SCREEN_BODY_Y = 84;       // Players, QR code or info.
constexpr int16_t BUTTON_ROW_H = 60;        // Well above the 44 px minimum target.
constexpr int16_t BUTTON_ROW_Y = 172;       // The one row most screens use.
constexpr int16_t BUTTON_UPPER_ROW_Y = 104; // Second row (test and Table screens).

enum class TouchAction : uint8_t {
  None, Pair, Pause, Resume, EndMatch,
  // Between games: start (lobby), cancel the countdown, rematch or reset.
  StartGame, CancelStart, Rematch, ResetTable,
  // Lobby hold: send everyone back out of an unstarted lobby.
  ClearLobby,
  // In a game: the Table screen and its master pass (a stuck turn).
  OpenTable, MasterPass,
  // Presence code screen: cancel the code a phone asked for.
  CancelCode,
  // Screens that change no table state.
  OpenInfo, OpenQr, CloseScreen, QrWifi, QrPortal, QrSignIn,
  // Test harness screen (only while a harness is connected): no Intents.
  OpenTests, CloseTests, StopTest, RunRadioCheck, RunQuickGame, RunFullGame, RunRematchGame,
  RunSoak
};

// Code: a presence code a phone asked for, shown over any other screen.
// Table: in-game controls kept off the main row (master pass, End match).
enum class ScreenKind : uint8_t { Status, Info, Qr, Tests, Code, Table };

struct TouchButton {
  TouchAction action = TouchAction::None;
  const char *label = "";
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  // Hold buttons act once held this long; 0 means the button acts on release.
  uint32_t holdMs = 0;
  // Shown as the current choice (the selected QR code).
  bool selected = false;

  bool hold() const { return holdMs > 0; }

  // slop widens the button on every side (for a press already on it).
  bool contains(int16_t px, int16_t py, int16_t slop = 0) const {
    return px >= x - slop && px < x + w + slop && py >= y - slop && py < y + h + slop;
  }
};

constexpr uint8_t MAX_TOUCH_BUTTONS = 6;
constexpr uint8_t SCREEN_NAME_LENGTH = 12;
constexpr uint8_t MAX_SCREEN_PLAYERS = 8;

// Player chip flags. Every one is also spelled out on the chip in words or a
// shape, never by color alone.
constexpr uint8_t CHIP_ACTIVE = 0x01;   // Whose turn it is.
constexpr uint8_t CHIP_OUT = 0x02;      // Eliminated.
// 0x04 was CHIP_HOST (no table host since 2026-09-25).
constexpr uint8_t CHIP_WINNER = 0x08;
constexpr uint8_t CHIP_STARTER = 0x10;  // Starts the next game.
constexpr uint8_t CHIP_WAITING = 0x20;  // Atlas is waiting on this player (win confirmation).

struct ScreenPlayer {
  uint8_t number = 0;
  char name[SCREEN_NAME_LENGTH + 1] = {};
  int32_t life = 0;
  uint8_t flags = 0;
};

// Everything the TFT shows. Equal screens need no redraw; atlas_display.cpp
// compares region by region.
struct AtlasScreen {
  ScreenKind kind = ScreenKind::Status;
  char badge[12] = {};    // State word in the header (LOBBY, PLAYING, ...).
  char title[32] = {};
  char detail[48] = {};
  char notice[48] = {};   // Action message; shown in place of detail while set.
  bool sdMissing = false; // Header warning: "NO SD CARD".
  uint8_t sigilsOnline = 0;

  ScreenPlayer players[MAX_SCREEN_PLAYERS];
  uint8_t playerCount = 0;
  bool showLife = false;

  // Turn clock: timerPermille is the countdown left (0-1000), or -1 with no
  // countdown; clock is "1:23" (left, or elapsed with no countdown).
  int16_t timerPermille = -1;
  bool timerWarning = false;
  char clock[8] = {};

  // Body text lines (info screen, or the lobby's join hint) and a QR code.
  char lines[5][40] = {};
  uint8_t lineCount = 0;
  char qr[112] = {};
  char qrCaption[40] = {};
  char code[8] = {};      // Presence code as "123 456" (Code screen).

  TouchButton buttons[MAX_TOUCH_BUTTONS];
  uint8_t buttonCount = 0;
  // The button under a finger right now, a hold's whole seconds left, and its
  // progress (0-1000) for the fill bar.
  TouchAction pressed = TouchAction::None;
  uint8_t holdSecondsLeft = 0;
  uint16_t holdPermille = 0;
};

bool sameHeader(const AtlasScreen &a, const AtlasScreen &b);
bool sameHero(const AtlasScreen &a, const AtlasScreen &b);
bool sameTimer(const AtlasScreen &a, const AtlasScreen &b);
bool sameBody(const AtlasScreen &a, const AtlasScreen &b);
bool samePlayer(const ScreenPlayer &a, const ScreenPlayer &b);
bool sameButtons(const AtlasScreen &a, const AtlasScreen &b);
bool sameScreen(const AtlasScreen &a, const AtlasScreen &b);

void buildAtlasScreen(uint32_t nowMs, AtlasScreen &screen);

// Touch adapter: one sample per poll, in screen coordinates. It only
// dispatches Intents; audit_adapters.py checks it.
void updateTouchControls(uint32_t nowMs, bool touched, int16_t x, int16_t y);

// Touch calibration may only take over the screen between games, in the lobby.
bool touchCalibrationAllowed();

// Clears press, notice and screen state (tests, and boot).
void resetTouchControls();

}  // namespace TurnHubAtlas
