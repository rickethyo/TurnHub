#pragma once

#include <stdint.h>

// Atlas's built-in 2.8" TFT. Presentation only: it renders Atlas state and
// never decides game outcomes. Firmware-only; host tests use a no-op stub.

namespace TurnHubAtlas {

// Brings up the panel, backlight and touch controller and draws the TurnHub
// splash.
void beginAtlasDisplay();
// Polls touch and redraws the status screen when it changes. Call every loop.
void serviceAtlasDisplay(uint32_t nowMs);
// Menu > Device Sleep (scheduled by handleSleepIntent, table_intents.cpp):
// screen dark, outputs held off, then deep sleep until the screen is touched
// (the XPT2046 pen interrupt, GPIO36) or BOOT (GPIO0) is pressed. Never
// returns: waking restarts Atlas.
void sleepAtlas();
// First thing in setup(): after a Sleep wake, gives the wake pins back to the
// digital GPIO driver and releases the held outputs.
void releaseSleepWakePins();

}  // namespace TurnHubAtlas
