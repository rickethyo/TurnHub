#pragma once

// Atlas touchscreen: what the built-in TFT shows and what its touch buttons
// do. Hardware-independent so host tests drive it: atlas_display.cpp feeds
// touch samples in and draws the AtlasScreen this module builds.
//
// The touch adapter only builds Intents with IntentOrigin::AtlasHardware (the
// screen is part of the Atlas and its only physical input); Atlas's handlers
// decide every outcome. Its non-Intent actions change no table state: the
// presence code screen (canceling a code a phone asked for), moving between screens
// (Home, Game, Table, Settings and their lists) and the confirm popup.
//
// Nothing acts on a hold (owner, 2026-10-30): an action that ends, removes or
// erases something asks first in a confirm popup, and everything else is a
// tap. Each destination has one path: the tab bar's four pages, and lists
// under Settings.

#include <Arduino.h>
#include "player_controls.h"
#include <string.h>

#include "mana_tap.h"
#include "update_notice.h"
#include "device_navigation.h"

namespace TurnHubAtlas {

// Portrait screen size in pixels: the S3 Atlas's 4-inch panel, drawn 1:1.
constexpr int16_t ATLAS_SCREEN_WIDTH = 320;
constexpr int16_t ATLAS_SCREEN_HEIGHT = 480;

// A touch counts as released only after this long without contact, because
// resistive panels drop out briefly during a press.
constexpr uint32_t TOUCH_RELEASE_MS = 60;
// A press that started on a button stays on it while within this many pixels
// of its edge, so resistive jitter and a rolling fingertip do not cancel it.
constexpr int16_t TOUCH_SLOP_PX = 12;
// How long an action message stays on screen.
constexpr uint32_t TOUCH_NOTICE_MS = 4000;

// Screen regions (atlas_display.cpp draws them; buttons live in the rows).
constexpr int16_t SCREEN_HEADER_H = 32;
constexpr int16_t SCREEN_HERO_Y = 36;       // Title and detail/notice line.
constexpr int16_t SCREEN_BODY_Y = 104;      // Players, QR code or info.
constexpr int16_t BUTTON_ROW_H = 56;        // Well above the 44 px minimum target.
constexpr int16_t BUTTON_ROW_Y = 368;       // The one row most screens use, at the bottom.
constexpr int16_t BUTTON_UPPER_ROW_Y = 304; // Second row, just above it (within thumb reach).
// In a game, the top of a player's tile (token and name) opens their screen;
// the rest is split into life down (left) and up (right).
constexpr int16_t CHIP_NAME_BAND_H = 32;
// The confirm popup's card, and the two buttons inside it.
constexpr int16_t CONFIRM_CARD_Y = 140;
constexpr int16_t CONFIRM_CARD_H = 236;
constexpr int16_t CONFIRM_BUTTON_Y = CONFIRM_CARD_Y + CONFIRM_CARD_H - BUTTON_ROW_H - 16;
// Settings lists: rows this tall at most, packed into one grouped card.
constexpr int16_t LIST_ROW_H = 56;

enum class TouchAction : uint8_t {
  None, Pair, Pause, Resume, EndMatch,
  OpenHome, OpenGame, OpenOverview, OpenSettings, OverviewNext,
  // Between games: start (lobby), cancel the countdown, rematch or reset.
  StartGame, CancelStart, Rematch, ResetTable,
  // Lobby: send everyone back out of an unstarted lobby (confirmed).
  ClearLobby,
  // In a game: the Table screen and its master pass (a stuck turn).
  OpenTable, MasterPass,
  OpenPlayerControls, PlayerControlsBack, PlayerControlsPrev, PlayerControlsNext, ControlMinus, ControlPlus,
  // Presence code screen: cancel the code a phone asked for.
  CancelCode,
  // Screens that change no table state.
  OpenQr, CloseScreen, QrWifi, QrPortal, QrSignIn,
  // Pairing v2 code check: the codes on the Sigil and Atlas match, or not.
  PairConfirm, PairReject,
  // In a game: tap a player's chip for that player's screen, which changes
  // their life (ChangeLife) and concedes for them (Concede, confirmed).
  OpenPlayer, LifeMinus5, LifeMinus1, LifePlus1, LifePlus5, Concede,
  // In the lobby, the same screen sets turn order (MoveSeat).
  MoveEarlier, MoveLater, SeatBLeft, SeatBRight,
  // ...and takes that one seat out of the lobby (RemoveSeat, confirmed).
  RemoveSeat,
  // First-run setup (FIRST_RUN_SETUP.md). Skip for now hides the Welcome
  // screen until the next start-up (presentation only); Setup under Settings
  // brings it back. Pair a Sigil and Done leave "You're all set"
  // (AdvanceSetup to Complete); Pair a Sigil also opens pairing.
  SkipSetup, OpenSetup, SetupPair, SetupDone,
  // Settings > Device: unpair every Sigil (lobby only) or factory reset
  // Atlas, each confirmed (the BOOT button's held gestures); Sleep until the
  // screen is touched.
  OpenDevice, UnpairSigils, FactoryResetAtlas, SleepAtlas,
  // ...and USB drive (a tap) lends the microSD card to a computer on USB-C,
  // or takes it back (sd_card.h).
  UsbDrive,
  // Settings > Home Wi-Fi: a tap turns the optional home network check-ins
  // on or off (home_wifi.h); the network itself is set in the portal or app.
  HomeWifi,
  OpenThemes, ThemeGraphite, ThemeDaylight, ThemeBrass, ThemeContrast, InvertDisplay,
  // In a game, like the app's table: the lower halves of a player's tile take
  // a life off or add one (ChangeLife for that player), its name band opens
  // their screen, and Pass passes the active player's turn (Pass, which keeps
  // its grace period for the seat to cancel).
  ChipLifeMinus, ChipLifePlus, PassTurn,
  // Lobby: seat a player from this screen, as the app's shared table does.
  // Add player lists the saved profiles not at a table (SeatProfile, arg =
  // row on the page; More pages on); one that needs a PIN asks for it on a
  // keypad (PinKey, arg = digit, PinBack, PinDone). New player types a name
  // (NameKey, arg = character, NameBack, NameDone) for a profile with no
  // PIN. Each seat is a JoinProfile Intent; Atlas decides.
  OpenSeat, SeatProfile, SeatMore, NewPlayer, NameKey, NameBack, NameDone, PinKey, PinBack, PinDone,
  // The confirm popup: do the action it asked about, or not.
  ConfirmYes, ConfirmNo,
  // Lobby: Start opens New game, the app's game setup (ConfigureGame from
  // the table): each row steps its setting, then Start game.
  OpenNewGame, GameType, GameMode, GameLife, GameTimer, GameHouseRules,
  // Mana Tap (mana_tap.h) from Home: open it, its Start/Pause/Resume/Play
  // again button, and Quit. Presentation only: no table state changes.
  OpenManaTap, ManaAction, ManaQuit
};

// Home: the table's status and info (version, Wi-Fi, SD card, battery).
// Code: a presence code a phone asked for, shown over any other screen.
// Table: in-game controls kept off the main row (master pass, End match).
// Menu: the Settings tab, one list (Pair a Sigil, QR codes, Theme, Device).
// PairCode: a Sigil waiting for the owner's pairing-code check (lobby only).
// Player: one player's screen, from their chip: life and Concede in a game,
// turn order in the lobby.
// Setup: first-run setup in the lobby, Welcome or "You're all set".
// Device: under Settings, USB drive, Sleep, Unpair Sigils and Factory reset.
// Seat, Name and Pin: Add player's profile list, name keyboard and PIN keypad.
// Confirm: the popup asking before a hazardous action.
// NewGame: the lobby's game setup before Start game.
// ManaTap: the mini game, opened from Home.
enum class ScreenKind : uint8_t { Home, Overview, Status, Qr, Code, Table, Menu, PairCode, Player, Setup, Device, Themes, Seat, Name, Pin, Confirm, NewGame, ManaTap };

struct TouchButton {
  TouchAction action = TouchAction::None;
  const char *label = "";
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  // Asks in the confirm popup before it acts (a hazardous action).
  bool confirm = false;
  // Shown as the current choice (the selected QR code).
  bool selected = false;
  // Which key or list row, for actions several buttons share (NameKey...).
  uint8_t arg = 0;

  // slop widens the button on every side (for a press already on it).
  bool contains(int16_t px, int16_t py, int16_t slop = 0) const {
    return px >= x - slop && px < x + w + slop && py >= y - slop && py < y + h + slop;
  }
};

// The name keyboard: 29 keys, then Cancel, Delete and Add.
constexpr uint8_t MAX_TOUCH_BUTTONS = 32;
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

// Where player chip `index` of `count` sits in the body. The drawing and the
// touch adapter share it, so a tap lands on the chip the player sees.
inline void screenChipCell(uint8_t index, uint8_t count, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  constexpr int16_t PAD = 8;
  constexpr int16_t GAP = 6;
  constexpr int16_t BODY_H = BUTTON_ROW_Y - 4 - SCREEN_BODY_Y;
  // Up to three players stack full width; more fill two columns.
  const uint8_t cols = count <= 3 ? 1 : 2;
  const uint8_t rows = count == 0 ? 1 : (count + cols - 1) / cols;
  w = (ATLAS_SCREEN_WIDTH - 2 * PAD - (cols - 1) * GAP) / cols;
  h = (BODY_H - (rows - 1) * GAP) / rows;
  x = PAD + (index % cols) * (w + GAP);
  y = SCREEN_BODY_Y + (index / cols) * (h + GAP);
}

struct ScreenPlayer {
  uint8_t number = 0;
  char name[SCREEN_NAME_LENGTH + 1] = {};
  int32_t life = 0;
  uint8_t flags = 0;
  uint8_t avatar = 0;  // Preset or approved custom image; 0 none.
  uint32_t imageRevision = 0;
  const uint8_t *avatarPixels = nullptr;  // Stable profile cache, RGB332 16px thumbnail.
  // In a game: this player's time on their own turns so far, the current
  // turn included ("12:40"); empty between games.
  char turnTime[9] = {};
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
  bool sdOnUsb = false;   // ...or "USB DRIVE" while a computer has the card.
  // Battery charge estimate (0-100), or -1 with no cell; the header shows it
  // as a cell icon and percent, and warns "LOW BATTERY" while batteryLow is set.
  int8_t batteryPercent = -1;
  bool batteryLow = false;
  bool batteryCharging = false;  // On USB: a lightning bolt over the cell icon.
  TurnHub::UpdateKind update = TurnHub::UpdateKind::None;  // Header pill: "Update available".
  uint8_t sigilsOnline = 0;
  // In a game: the round (the starter's turn opens each one; 0 between
  // games) and the match's running time ("42:10"), shown in the header.
  uint16_t round = 0;
  char gameClock[10] = {};

  ScreenPlayer players[MAX_SCREEN_PLAYERS];
  uint8_t playerCount = 0;
  bool showLife = false;
  bool showPlayerControls = false;
  uint8_t controlsPage = 0;
  TurnHubUi::PlayerControls controls{};

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

  // Mana Tap: a copy of the game (ManaTap screen only), and whether it
  // paused itself because a turn passed.
  TurnHub::ManaTap mana;
  bool manaTurnPaused = false;

  TouchButton buttons[MAX_TOUCH_BUTTONS];
  uint8_t buttonCount = 0;
  // The button under a finger right now.
  TouchAction pressed = TouchAction::None;
  uint8_t pressedArg = 0;
};

// Region comparisons for the display, which redraws only what changed.
// samePlayer ignores turnTime, which ticks every second: the display
// redraws that one line in place (samePlayerTime) instead of the whole chip.
// Likewise sameHeader ignores the game clock (sameGameClock).
inline bool sameScreenText(const char *a, const char *b) { return strcmp(a, b) == 0; }

inline bool samePlayer(const ScreenPlayer &a, const ScreenPlayer &b) {
  return a.number == b.number && a.life == b.life && a.flags == b.flags && a.avatar == b.avatar && a.imageRevision == b.imageRevision &&
      sameScreenText(a.name, b.name);
}

inline bool samePlayerTime(const ScreenPlayer &a, const ScreenPlayer &b) {
  return sameScreenText(a.turnTime, b.turnTime);
}

inline bool sameHeader(const AtlasScreen &a, const AtlasScreen &b) {
  return sameScreenText(a.badge, b.badge) && a.sdMissing == b.sdMissing && a.sdOnUsb == b.sdOnUsb &&
      a.batteryPercent == b.batteryPercent && a.batteryLow == b.batteryLow &&
      a.batteryCharging == b.batteryCharging &&
      a.update == b.update && a.sigilsOnline == b.sigilsOnline &&
      a.round == b.round;
}

inline bool sameGameClock(const AtlasScreen &a, const AtlasScreen &b) {
  return sameScreenText(a.gameClock, b.gameClock);
}

inline bool sameHero(const AtlasScreen &a, const AtlasScreen &b) {
  return a.kind == b.kind && sameScreenText(a.title, b.title) && sameScreenText(a.detail, b.detail) &&
      sameScreenText(a.notice, b.notice);
}

inline bool sameTimer(const AtlasScreen &a, const AtlasScreen &b) {
  return a.timerPermille == b.timerPermille && a.timerWarning == b.timerWarning && sameScreenText(a.clock, b.clock);
}

// Mana Tap's moving parts (orbs, score, lives); the display moves them in
// place (updateAtlasScreenMana). Its phase is part of sameBody.
inline bool sameMana(const AtlasScreen &a, const AtlasScreen &b) {
  const TurnHub::ManaTap &x = a.mana, &y = b.mana;
  if (x.score() != y.score() || x.lives() != y.lives() || x.best() != y.best()) return false;
  for (uint8_t i = 0; i < TurnHub::MANA_ORBS; ++i) {
    const TurnHub::ManaOrb &p = x.orb(i), &q = y.orb(i);
    if (p.live != q.live || (p.live && (p.target != q.target || p.seat != q.seat || p.x != q.x || p.y() != q.y())))
      return false;
  }
  return true;
}

// times: compare the players' turn times too (otherwise only whether each has one).
inline bool sameBody(const AtlasScreen &a, const AtlasScreen &b, bool times = true) {
  if (a.mana.phase() != b.mana.phase() || a.manaTurnPaused != b.manaTurnPaused ||
      a.showPlayerControls != b.showPlayerControls || a.controlsPage != b.controlsPage ||
      memcmp(&a.controls,&b.controls,sizeof(a.controls)) || a.kind != b.kind || a.playerCount != b.playerCount || a.showLife != b.showLife ||
      a.lineCount != b.lineCount || !sameScreenText(a.qr, b.qr) || !sameScreenText(a.qrCaption, b.qrCaption) ||
      !sameScreenText(a.code, b.code)) {
    return false;
  }
  for (uint8_t i = 0; i < a.lineCount; ++i) if (!sameScreenText(a.lines[i], b.lines[i])) return false;
  for (uint8_t i = 0; i < a.playerCount; ++i) {
    if (!samePlayer(a.players[i], b.players[i])) return false;
    if (times ? !samePlayerTime(a.players[i], b.players[i])
              : (a.players[i].turnTime[0] != 0) != (b.players[i].turnTime[0] != 0)) return false;
  }
  return true;
}

inline bool sameButtons(const AtlasScreen &a, const AtlasScreen &b) {
  if (a.buttonCount != b.buttonCount || a.pressed != b.pressed || a.pressedArg != b.pressedArg) {
    return false;
  }
  for (uint8_t i = 0; i < a.buttonCount; ++i) {
    const TouchButton &x = a.buttons[i];
    const TouchButton &y = b.buttons[i];
    if (x.action != y.action || x.arg != y.arg || x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h ||
        x.selected != y.selected || x.confirm != y.confirm || strcmp(x.label, y.label) != 0) {
      return false;
    }
  }
  return true;
}

inline bool sameScreen(const AtlasScreen &a, const AtlasScreen &b) {
  return sameHeader(a, b) && sameGameClock(a, b) && sameHero(a, b) && sameTimer(a, b) && sameBody(a, b) &&
      sameButtons(a, b) && sameMana(a, b);
}

// Equal but for the ticking text (game clock, turn clock and its ring, turn
// times) and Mana Tap's moving orbs: the display updates those in place
// instead of rebuilding the screen, so a running clock never redraws the
// whole panel.
inline bool sameScreenButTimes(const AtlasScreen &a, const AtlasScreen &b) {
  return sameHeader(a, b) && sameHero(a, b) && sameButtons(a, b) && sameBody(a, b, false) &&
      (a.gameClock[0] != 0) == (b.gameClock[0] != 0) && (a.clock[0] != 0) == (b.clock[0] != 0) &&
      a.timerWarning == b.timerWarning && (a.timerPermille < 0) == (b.timerPermille < 0);
}

void buildAtlasScreen(uint32_t nowMs, AtlasScreen &screen);

// Touch adapter: one sample per poll, in screen coordinates. It only
// dispatches Intents; audit_adapters.py checks it.
void updateTouchControls(uint32_t nowMs, bool touched, int16_t x, int16_t y);


// Clears press, notice and screen state (tests, and boot).
void resetTouchControls(bool startAtHome = false);

}  // namespace TurnHubAtlas
