#include "sigil_display.h"

#ifndef TURNHUB_DISPLAY_OLED
#define TURNHUB_DISPLAY_OLED 0
#endif

#if TURNHUB_DISPLAY_OLED == 1
#include "oled_display.h"
#elif TURNHUB_DISPLAY_OLED == 0
#include "epaper_display.h"
#else
#error "TURNHUB_DISPLAY_OLED must be 0 (e-paper) or 1 (OLED)"
#endif

namespace TurnHubSigil {

SigilDisplay &getSigilDisplay() {
#if TURNHUB_DISPLAY_OLED
  static OledDisplay display;
#else
  static EpaperDisplay display;
#endif
  return display;
}

}  // namespace TurnHubSigil
