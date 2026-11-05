#pragma once

#include <stdint.h>
#include "device_theme.h"

// Atlas's built-in 2.8" TFT. Presentation only: it renders Atlas state and
// never decides game outcomes. Firmware-only; host tests use a no-op stub.

namespace TurnHubAtlas {

// Brings up the panel, backlight and touch controller and draws the TurnHub
// splash.
void beginAtlasDisplay();
// Polls touch and redraws the status screen when it changes. Call every loop.
void serviceAtlasDisplay(uint32_t nowMs);
bool startAtlasScreenTest();
TurnHubTheme::Id atlasDisplayTheme();
bool chooseAtlasDisplayTheme(TurnHubTheme::Id theme);
bool atlasDisplayInverted();
bool chooseAtlasDisplayInverted(bool inverted);
// Backlight off and panel asleep, just before deep sleep (sleepAtlas).
void darkenAtlasDisplayForSleep();
// Menu > Device Sleep (scheduled by handleSleepIntent, device_intents.cpp):
// screen dark, backlight held off, then deep sleep until the screen is
// touched (the FT6336U interrupt, TOUCH_INT_PIN) or BOOT (GPIO0) is pressed. Never
// returns: waking restarts Atlas. Asleep on the cell, Atlas also wakes every
// minute to look for USB (stayAsleepWithoutUsb) and starts when it is back.
// batteryEmpty: the cell is flat, so a touch or BOOT doesn't start Atlas
// either; only USB does.
void sleepAtlas(bool batteryEmpty);
// Before anything else in setup(): after a wake from sleep on the cell that
// should not start Atlas (the minute's USB check, or a touch on an empty
// cell) with USB still absent, goes straight back to sleep without returning.
void stayAsleepWithoutUsb();
// First thing in setup() after that: after a Sleep wake, gives the wake pins
// back to the digital GPIO driver and releases the held outputs.
void releaseSleepWakePins();
}  // namespace TurnHubAtlas
