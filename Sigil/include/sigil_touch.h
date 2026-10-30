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
void sigilTouchStartCalibration();
bool sigilTouchCalibrating();

}  // namespace TurnHubSigil
