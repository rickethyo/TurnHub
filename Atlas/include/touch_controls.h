#pragma once

// Atlas touchscreen: what the built-in TFT shows and what its touch buttons
// do. Hardware-independent so host tests drive it: atlas_display.cpp feeds
// touch samples in and draws the AtlasScreen this module builds.
//
// The touch adapter only builds Intents with IntentOrigin::AtlasHardware (the
// screen is part of the Atlas and its only physical input); Atlas's handlers
// decide every outcome. Its one non-Intent action is the admin unlock window,
// a physical-presence proof like the master button it replaced.

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

enum class TouchAction : uint8_t {
  None, Pair, Pass, Pause, Resume, EndMatch, UnlockAdmin, LockAdmin,
  // Test harness screen (only while a harness is connected): no Intents.
  OpenTests, CloseTests, StopTest, RunRadioCheck, RunQuickGame, RunFullGame, RunRematchGame,
  RunSoak
};

struct TouchButton {
  TouchAction action = TouchAction::None;
  const char *label = "";
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  // Hold buttons act once held this long; 0 means the button acts on release.
  uint32_t holdMs = 0;

  bool hold() const { return holdMs > 0; }

  // slop widens the button on every side (for a press already on it).
  bool contains(int16_t px, int16_t py, int16_t slop = 0) const {
    return px >= x - slop && px < x + w + slop && py >= y - slop && py < y + h + slop;
  }
};

constexpr uint8_t MAX_TOUCH_BUTTONS = 6;

// Everything the TFT shows. Equal screens need no redraw.
struct AtlasScreen {
  char title[32] = {};
  char detail[48] = {};
  char notice[48] = {};
  TouchButton buttons[MAX_TOUCH_BUTTONS];
  uint8_t buttonCount = 0;
  // The button under a finger right now, and a hold's whole seconds left.
  TouchAction pressed = TouchAction::None;
  uint8_t holdSecondsLeft = 0;
};

bool sameButtons(const AtlasScreen &a, const AtlasScreen &b);
bool sameScreen(const AtlasScreen &a, const AtlasScreen &b);

void buildAtlasScreen(uint32_t nowMs, AtlasScreen &screen);

// Touch adapter: one sample per poll, in screen coordinates. It only
// dispatches Intents; audit_adapters.py checks it.
void updateTouchControls(uint32_t nowMs, bool touched, int16_t x, int16_t y);

// Touch calibration may only take over the screen between games, in the lobby.
bool touchCalibrationAllowed();

// Clears press and notice state (tests, and boot).
void resetTouchControls();

}  // namespace TurnHubAtlas
