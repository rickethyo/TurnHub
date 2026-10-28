#pragma once

// The LVGL Sigil screen: 240 x 320 portrait, the five compass keys as
// touch buttons along the bottom (sigil_touch_pad.h), drawn in the app's
// style (design tokens, flat cards). Hardware-free: a board subclass creates
// the LVGL display and flushes it (sigil_board_e32r28t.cpp), and the host
// preview (tests/host/render-sigil-lvgl.sh) draws it to images.

#include <lvgl.h>

#include "sigil_display.h"
#include "sigil_touch_pad.h"

namespace TurnHubSigil {

// Labels for the five on-screen keys; nullptr leaves a key blank.
struct PadLabels {
  const char *text[KEY_COUNT] = {};
  const char *note[KEY_COUNT] = {};  // Small second line ("HOLD", a tag).
  bool accent[KEY_COUNT] = {};
};

class LvglSigilDisplay : public SigilDisplay {
 public:
  void begin() override {}
  void setTheme(TurnHubTheme::Id theme) override;
  void setInverted(bool inverted) override;
  void showBooting() override;
  void showUnpaired() override;
  void showReady(uint8_t sigilId) override;
  void showAtlasLost(uint8_t sigilId) override;
  void showPairingCode(uint16_t code) override;
  void showUpdate(const char *status, int8_t percent) override;
  void showSleeping() override;
  bool setSeatName(uint8_t slot, const char *name) override;
  void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot) override;
  void showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode, uint8_t primaryPlayer,
      uint8_t secondaryPlayer, uint8_t turnNumber, uint8_t flags) override;
  void showCommander(const TurnHubProtocol::CommanderFlowPacket &page) override;
  void showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t cursor) override;

  // The key a finger is on (Key::Count for none), lit on the pad.
  void setPressedKey(Key key);

 protected:
  // Called after each screen is built; the board pushes it to the panel.
  virtual void present() {}

 private:
  lv_obj_t *frame(const char *title, uint8_t sigilId = 0xFF, uint8_t turn = 0);
  void pad(const PadLabels &labels);
  PadLabels menuLabels() const;
  bool drawDeviceMenu(uint8_t sigilId);
  void statusScreen(const char *title, const char *line1, const char *line2, uint8_t sigilId,
      PadLabels labels);
  void finish(const PadLabels &labels);
  bool lifeRequestShown() const { return life_.request.target != 0; }
  bool lifeKeysShown() const;

  char seatName_[2][TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  lv_obj_t *keys_[KEY_COUNT] = {};
  Key pressed_ = Key::Count;
};

}  // namespace TurnHubSigil
