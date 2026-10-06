#include "sigil_display.h"

#ifndef TURNHUB_DISPLAY_OLED
#define TURNHUB_DISPLAY_OLED 0
#endif

#ifndef TURNHUB_SPARE
#define TURNHUB_SPARE 0
#endif

#if TURNHUB_SPARE
// The spare build drives no panel (SPARE_SIGIL.md).
#elif TURNHUB_DISPLAY_OLED == 1
#include "oled_display.h"
#elif TURNHUB_DISPLAY_OLED == 0
#include "epaper_display.h"
#else
#error "TURNHUB_DISPLAY_OLED must be 0 (e-paper) or 1 (OLED)"
#endif

namespace TurnHubSigil {

#if TURNHUB_SPARE
namespace {
class NoDisplay : public SigilDisplay {
 public:
  void begin() override {}
  void showBooting() override {}
  void showUnpaired() override {}
  void showReady(uint8_t) override {}
  void showAtlasLost(uint8_t) override {}
  void showPairingCode(uint16_t) override {}
  void showUpdate(const char *, int8_t) override {}
  bool setSeatName(uint8_t, const char *) override { return false; }
  void showGame(const TurnHubProtocol::GameDisplayPacket &) override {}
  void showState(uint8_t, TurnHubProtocol::DisplayMode, uint8_t, uint8_t, uint8_t, uint8_t) override {}
};
}  // namespace
#endif

SigilDisplay &getSigilDisplay() {
#if TURNHUB_SPARE
  static NoDisplay display;
#elif TURNHUB_DISPLAY_OLED
  static OledDisplay display;
#else
  static EpaperDisplay display;
#endif
  return display;
}

}  // namespace TurnHubSigil
