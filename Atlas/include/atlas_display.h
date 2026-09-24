#pragma once

// Atlas's built-in 2.8" TFT. Presentation only: it renders Atlas state and
// never decides game outcomes. Firmware-only; host tests use a no-op stub.

namespace TurnHubAtlas {

// Brings up the panel and backlight and draws the TurnHub splash.
void beginAtlasDisplay();

}  // namespace TurnHubAtlas
