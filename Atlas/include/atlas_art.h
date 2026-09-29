#pragma once

// Atlas touchscreen art (atlas_art.cpp): the Brass theme's drawing of the
// AtlasScreen and the splash onto a LovyanGFX target. atlas_display.cpp
// gives it the panel; the host preview gives it a sprite.

#include <stdint.h>

#include "touch_controls.h"

namespace lgfx { inline namespace v1 { class LovyanGFX; } }

namespace TurnHubAtlas {

// Brass palette (RGB888), shared with the calibration screens.
constexpr uint32_t WALNUT = 0x140F0A;      // Background.
constexpr uint32_t PLATE = 0x241C14;       // Chips and secondary buttons.
constexpr uint32_t PLATE_HI = 0x33271B;    // The active player's or winner's chip.
constexpr uint32_t LINE = 0x6E5530;        // Plate frames.
constexpr uint32_t LINE_OUT = 0x3D3020;    // An eliminated player's frame.
constexpr uint32_t LINE_HI = 0xA8813F;     // Button frames.
constexpr uint32_t BRASS = 0xE0A944;
constexpr uint32_t BRASS_HI = 0xF8D98F;
constexpr uint32_t BRASS_LO = 0xA06D1F;
constexpr uint32_t BRASS_DEEP = 0x5B3E12;
constexpr uint32_t CREAM = 0xF6ECD9;       // Main text.
constexpr uint32_t MUTED = 0xC9B594;       // Detail text.
constexpr uint32_t FAINT = 0x97866B;
constexpr uint32_t INK = 0x1C1205;         // Text on brass.
constexpr uint32_t DIAL = 0xF1E4C6;        // Gauge face.
constexpr uint32_t DIAL_INK = 0x3B2A14;
constexpr uint32_t TUBE = 0x0A0705;
constexpr uint32_t DANGER = 0xE0503F;
constexpr uint32_t DANGER_DEEP = 0x7A1F18;
constexpr uint32_t DIM = 0x5D5040;         // Eliminated players.
constexpr uint32_t QR_LIGHT = 0xFFFFFF;

struct AtlasArtStatus {
  bool fonts = false;    // Every Brass font loaded (else DejaVu stands in).
  bool buffers = false;  // The header and gauge sprites were allocated.
};

// Loads the fonts and allocates the off-screen buffers; call once.
AtlasArtStatus beginAtlasArt(lgfx::LovyanGFX &panel);
void drawAtlasSplash();
// Draws the screen, redrawing only the regions that changed since the last
// call, and keeps the header gear turning while a game runs.
void renderAtlasScreen(const AtlasScreen &screen, uint32_t nowMs);
// The next render redraws everything (after another screen covered it).
void invalidateAtlasScreen();

}  // namespace TurnHubAtlas
