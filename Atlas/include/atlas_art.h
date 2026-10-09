#pragma once

// Atlas touchscreen art (atlas_art.cpp): the selected theme's drawing of the
// AtlasScreen and the splash onto a LovyanGFX target. atlas_display.cpp
// gives it the panel; the host preview gives it a sprite.

#include <stdint.h>
#include "device_theme.h"

#include "touch_controls.h"

namespace lgfx { inline namespace v1 { class LovyanGFX; } }

namespace TurnHubAtlas {

// Active theme colors (RGB888); calibration uses the same palette.
extern uint32_t WALNUT;
extern uint32_t PLATE;
extern uint32_t PLATE_HI;
extern uint32_t LINE;
extern uint32_t LINE_OUT;
extern uint32_t LINE_HI;
extern uint32_t BRASS;
extern uint32_t BRASS_HI;
extern uint32_t BRASS_LO;
extern uint32_t BRASS_DEEP;
extern uint32_t CREAM;
extern uint32_t MUTED;
extern uint32_t FAINT;
extern uint32_t INK;
extern uint32_t DIAL;
extern uint32_t DIAL_INK;
extern uint32_t TUBE;
extern uint32_t DANGER;
extern uint32_t DANGER_DEEP;
extern uint32_t UPDATE_BLUE;
extern uint32_t UPDATE_BLUE_EDGE;
extern uint32_t DIM;
constexpr uint32_t QR_LIGHT = 0xFFFFFF;
void setAtlasArtTheme(TurnHubTheme::Id theme);
TurnHubTheme::Id atlasArtTheme();
void setAtlasArtInverted(bool inverted);
bool atlasArtInverted();

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
