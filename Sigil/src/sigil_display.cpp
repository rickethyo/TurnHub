#include "sigil_display.h"

// The touch Sigil (env:sigil-e32r28t) has its own, in sigil_board_e32r28t.cpp.
#if !defined(TURNHUB_BOARD_E32R28T)

namespace TurnHubSigil {

namespace {
// The Gen 1 e-paper and OLED drivers live on legacy/gen1. Until the LVGL Sigil
// renderer (LVGL/) is bound to this interface, the unified Sigil is headless:
// the status ring, buzzer and serial log still report state.
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

SigilDisplay &getSigilDisplay() {
  static NoDisplay display;
  return display;
}

}  // namespace TurnHubSigil

#endif
