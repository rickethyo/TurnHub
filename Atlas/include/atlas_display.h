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

}  // namespace TurnHubAtlas
