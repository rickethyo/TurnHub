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
void renderAtlasSplash(const ScreenStyle &style, const char *firmware);

}  // namespace TurnHubAtlas
