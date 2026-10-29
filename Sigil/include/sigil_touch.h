#pragma once

// The touch Sigil's panel input (sigil_board_e32r28t.cpp): which on-screen
// key (sigil_touch_pad.h) a finger is on. main.cpp polls it from loop() and
// feeds it to the same debounced keys as the thumbstick.

#include "sigil_menu.h"

namespace TurnHubSigil {

void sigilTouchBegin();
// Reads the panel; Key::Count when nothing is pressed.
Key sigilTouchRead();
// Device menu > Calibrate (or the "touch cal" serial command): four
// crosses, saved to NVS. While it runs every read returns Key::Count.
void sigilTouchStartCalibration();
bool sigilTouchCalibrating();

}  // namespace TurnHubSigil
