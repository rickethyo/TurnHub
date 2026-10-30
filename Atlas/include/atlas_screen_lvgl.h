#pragma once

// Draws the Atlas screen model with LVGL on the active screen (S3 Atlas and
// the host preview renderer). See atlas_screen_lvgl.cpp.

#include <lvgl.h>

#include "device_theme.h"
#include "touch_controls.h"

namespace TurnHubAtlas {

struct ScreenStyle {
  TurnHubTheme::Id theme;
  bool inverted;
};

void renderAtlasScreen(const AtlasScreen &screen, const ScreenStyle &style);
// Updates the ticking labels of the screen last rendered in place (see
// sameScreenButTimes); false when it can't, and the caller renders afresh.
bool updateAtlasScreenTimes(const AtlasScreen &screen);
// Moves Mana Tap's orbs, score and lives on the screen last rendered; true
// when there is nothing to move, false when it can't (render afresh).
bool updateAtlasScreenMana(const AtlasScreen &screen);
void renderAtlasSplash(const ScreenStyle &style, const char *firmware);

}  // namespace TurnHubAtlas
