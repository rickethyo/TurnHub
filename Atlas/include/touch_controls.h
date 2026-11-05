#pragma once

// Atlas touchscreen: what the built-in TFT shows and what its touch buttons
// do. Hardware-independent so host tests drive it: atlas_display.cpp feeds
// touch samples in and draws the AtlasScreen this module builds.
//
// Games run on the hub phone (hosted_game.h): the Game tab shows its game,
// and its table controls queue inputs for the hub. Device actions build
// Intents with IntentOrigin::AtlasHardware (the screen is part of the Atlas
// and its only physical input); Atlas's handlers decide every outcome. Its
// other actions change no device state: the presence code screen (canceling a
// code a phone asked for), moving between screens (Home, Game, Settings and
// their lists) and the confirm popup.
//
// Nothing acts on a hold (owner, 2026-10-30): an action that ends, removes or
// erases something asks first in a confirm popup, and everything else is a
// tap. Each destination has one path: the tab bar's three pages, and lists
// under Settings.

#include <Arduino.h>
#include <string.h>

#include "mana_tap.h"
#include "card_cache.h"
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
// The player tile's name band above its life total.
constexpr int16_t CHIP_NAME_BAND_H = 32;
// The confirm popup's card, and the two buttons inside it.
constexpr int16_t CONFIRM_CARD_Y = 140;
constexpr int16_t CONFIRM_CARD_H = 236;
constexpr int16_t CONFIRM_BUTTON_Y = CONFIRM_CARD_Y + CONFIRM_CARD_H - BUTTON_ROW_H - 16;
// Settings lists: rows this tall at most, packed into one grouped card.
constexpr int16_t LIST_ROW_H = 56;

// Local device controls and the hosted game's supported table inputs.
enum class TouchAction : uint8_t {
  None, CancelCode, CloseScreen, ConfirmNo, ConfirmYes, FactoryResetAtlas,
  HomeWifi, InvertDisplay, ManaAction, ManaQuit, OpenDevice, OpenGame, OpenHome,
  OpenManaTap, OpenPower, OpenQr, OpenSettings, OpenSetup, OpenThemes,
  Pair, PairConfirm, PairReject, PassTurn, Pause, PowerAutoSleep, PowerGameScreen,
  PowerScreenOff, Resume, SetupDone, SetupPair, SkipSetup, SleepAtlas,
  SpeakerVolume, StartGame, UnpairSigils, UsbDrive,
  ThemeGraphite, ThemeDaylight, ThemeBrass, ThemeContrast,
  OpenPlanes, PlanarRoll, PlanarWalk, PlanarResolve, OpenPlaneCard, PlaneCardBack,
  // A phone asking to pair (phone_links.h), and Settings > Device's Forget phones.
  PhoneAllow, PhoneRefuse, ForgetPhones
};

// Main pages and overlays that the touch producer can actually build.
enum class ScreenKind : uint8_t { Home, Status, Qr, Code, Menu, PairCode, Setup, Device, Power, Themes, Confirm, ManaTap, Planes, PlaneCard, PhonePair };

struct TouchButton {
  TouchAction action = TouchAction::None;
  const char *label = "";
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  // Asks in the confirm popup before it acts (a hazardous action).
  bool confirm = false;
  // Shown as the current choice (a selected tab or theme).
  bool selected = false;
  // Distinguishes buttons that share an action.
  uint8_t arg = 0;

  // slop widens the button on every side (for a press already on it).
  bool contains(int16_t px, int16_t py, int16_t slop = 0) const {
    return px >= x - slop && px < x + w + slop && py >= y - slop && py < y + h + slop;
  }
};

// Bounded capacity for the content buttons and navigation tabs.
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

// Planechase on the screen (a match with Planechase on). The roll fields are
// set on every screen, so the display rolls its planar die over whatever is
// open whenever any player rolls (a Sigil, the app, a tablet or this screen);
// the rest is the Planes screen's plane card. Faces: 0 blank, 1 chaos,
// 2 planeswalk, as in the hub's game (hosted-v1.md).
constexpr uint8_t SCREEN_PLANE_NAME_LENGTH = 40;
struct ScreenPlanes {
  bool on = false;
  bool digital = false;      // Atlas's digital deck, else the players' own plane cards.
  bool phenomenon = false;
  char plane[SCREEN_PLANE_NAME_LENGTH + 1] = {};  // The face-up card's name (digital).
  int32_t deckLeft = 0, planeswalks = 0, chaos = 0, counters = 0, rollCost = 0;
  // The match's latest roll: its number (0 before the first), who, the face,
  // what it cost and whether a card made it (free).
  int32_t roll = 0;
  uint8_t face = 0, cost = 0;
  bool fromEffect = false;
  char roller[SCREEN_NAME_LENGTH + 1] = {};
  // Card view (PlaneCard): the saved card's text and its art, 480x320
  // RGB565 (nullptr without art). Both stay valid until the plane changes.
  const TurnHubCards::PlaneCard *card = nullptr;
  const uint16_t *art = nullptr;
  bool operator==(const ScreenPlanes &o) const {
    return on == o.on && digital == o.digital && phenomenon == o.phenomenon && !strcmp(plane, o.plane) &&
        deckLeft == o.deckLeft && planeswalks == o.planeswalks && chaos == o.chaos && counters == o.counters &&
        rollCost == o.rollCost && roll == o.roll && face == o.face && cost == o.cost && fromEffect == o.fromEffect &&
        !strcmp(roller, o.roller) && card == o.card && (!card || !strcmp(card->id, o.card->id)) && art == o.art;
  }
  bool operator!=(const ScreenPlanes &o) const { return !(*this == o); }
};

// Everything the TFT shows. Equal screens need no redraw; atlas_display.cpp
// compares region by region.
struct AtlasScreen {
  ScreenKind kind = ScreenKind::Status;
  char badge[12] = {};    // State word in the header (LOBBY, PLAYING, ...).
  char title[32] = {};
  char detail[48] = {};
  char notice[48] = {};   // Action message; shown in place of detail while set.
  char sync[48] = {};     // showSyncStatus's toast while it is up.
  bool sdMissing = false; // Header warning: "NO SD CARD".
  bool sdOnUsb = false;   // ...or "USB DRIVE" while a computer has the card.
  // Battery charge estimate (0-100), or -1 with no cell; the header shows it
  // as a cell icon and percent, and warns "LOW BATTERY" while batteryLow is set.
  int8_t batteryPercent = -1;
  bool batteryLow = false;
  bool batteryCharging = false;  // On USB: a lightning bolt over the cell icon.
  TurnHub::UpdateKind update = TurnHub::UpdateKind::None;  // Header pill: "Update available".
  uint8_t sigilsOnline = 0;
  // Header link icons (owner, 2026-11-03), each a shape with its own colour
  // and letter, so none relies on colour alone: the TurnHub hotspot (A, lit
  // while a phone is on it), home Wi-Fi (H, slashed while joining, hidden
  // when off) and the Sigil radio (S, lit while a Sigil is online).
  bool hotspotOn = true;  // Off on the home-only bench build: no A icon.
  uint8_t hotspotStations = 0;
  bool homeWifiOn = false;
  bool homeWifiLinked = false;
  // In a game: the round (the starter's turn opens each one; 0 between
  // games) and the match's running time ("42:10"), shown in the header.
  uint16_t round = 0;
  char gameClock[10] = {};

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
  char qr[256] = {};
  char qrCaption[40] = {};
  char code[8] = {};      // Presence code as "123 456" (Code screen).

  // Mana Tap: a copy of the game (ManaTap screen only), and whether it
  // paused itself because a turn passed.
  TurnHub::ManaTap mana;
  bool manaTurnPaused = false;


  ScreenPlanes planes;

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
      a.hotspotOn == b.hotspotOn && a.hotspotStations == b.hotspotStations && a.homeWifiOn == b.homeWifiOn && a.homeWifiLinked == b.homeWifiLinked &&
      a.round == b.round;
}

inline bool sameGameClock(const AtlasScreen &a, const AtlasScreen &b) {
  return sameScreenText(a.gameClock, b.gameClock);
}

inline bool sameHero(const AtlasScreen &a, const AtlasScreen &b) {
  return a.kind == b.kind && sameScreenText(a.title, b.title) && sameScreenText(a.detail, b.detail) &&
      sameScreenText(a.notice, b.notice) && sameScreenText(a.sync, b.sync);
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
      a.kind != b.kind || a.playerCount != b.playerCount || a.showLife != b.showLife ||
      a.lineCount != b.lineCount || !sameScreenText(a.qr, b.qr) || !sameScreenText(a.qrCaption, b.qrCaption) ||
      !sameScreenText(a.code, b.code) || a.planes != b.planes) {
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


// A message about data arriving from an app (the plane catalog, plane
// cards...), shown over any screen as a toast for TOUCH_NOTICE_MS after the
// latest one, so the table sees the update happen. Presentation only.
constexpr uint32_t SYNC_TOAST_MS = TOUCH_NOTICE_MS;
void showSyncStatus(uint32_t nowMs, const char *text);

// Clears press, notice and screen state (tests, and boot).
void resetTouchControls(bool startAtHome = false);

}  // namespace TurnHubAtlas
