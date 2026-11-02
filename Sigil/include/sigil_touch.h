#pragma once

// The touch Sigil's panel input (sigil_board_e32r28t.cpp): which on-screen
// key (sigil_touch_pad.h) a finger is on. main.cpp polls it from loop() and
// feeds it to the debounced touch-button actions.

#include "sigil_menu.h"

namespace TurnHubSigil {

void sigilTouchBegin();
// Reads the panel; Key::Count when nothing is pressed.
Key sigilTouchRead();
uint8_t sigilTouchMenuRevision();
uint8_t sigilTouchPlayer();
uint16_t sigilTouchLifeRequest();
// Where the finger is now, in screen pixels; false when nothing is pressed.
// Mana Tap aims at moving orbs, which no fixed key region can name.
bool sigilTouchPoint(int16_t &x, int16_t &y);
// Device menu > Calibrate (or the "touch cal" serial command): four
// crosses, saved to NVS. While it runs every read returns Key::Count.
// Screen off (owner 2026-11-02, as on Atlas): after SIGIL_SCREEN_OFF_IDLE_MS
// with no touch and nothing new to show, the backlight goes off; the radio
// and game keep running. A touch lights it without pressing anything.
// sigilScreenActivity lights it (something new to show); sigilScreenKeepOn
// keeps it lit (a match is running or paused).
constexpr uint32_t SIGIL_SCREEN_OFF_IDLE_MS = 2UL * 60UL * 1000UL;
void sigilScreenActivity();
void sigilScreenKeepOn(bool keepOn);
void sigilTouchStartCalibration();
bool sigilTouchCalibrating();

}  // namespace TurnHubSigil
