#pragma once

// The LVGL Sigil client: app-style navigation, personal player controls,
// read-only table and device settings. The board owns display flush and touch;
// the renderer publishes the exact visible hit regions with each frame.

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
  bool setSeatName(uint8_t slot, const char *name) override;
  void showPlayerControls(const TurnHubProtocol::GameDisplayPacket &, uint8_t page) override;
  void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot) override;
  void showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode, uint8_t primaryPlayer,
      uint8_t secondaryPlayer, uint8_t turnNumber, uint8_t flags) override;
  void showCommander(const TurnHubProtocol::CommanderFlowPacket &page) override;
  void showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t cursor) override;

  // The key a finger is on (Key::Count for none), lit on the pad.
  void setPressedKey(Key key);
  void showHome(uint8_t sigilId, bool connected, const char *name, uint8_t mode) override;
  void showTable(uint8_t sigilId, const TableSnapshot &table, uint8_t page, bool connected) override;
  void showSettings(uint8_t sigilId) override;
  const TouchMap &touchMap() const { return touchMap_; }

 protected:
  // Called after each screen is built; the board pushes it to the panel.
  virtual void present() {}

 private:
  lv_obj_t *frame(const char *title, uint8_t sigilId = 0xFF, uint8_t turn = 0, const char *subtitle = nullptr);
  void pad(const PadLabels &labels);
  void button(const char *label, const char *note, PadRect rect, uint8_t target,
      bool accent = false, uint32_t meaning = 0);
  void navigation(TurnHubUi::Page selected);
  PadLabels menuLabels() const;
  // Open Menu, as the app's player menu: who is the player's name (nullptr
  // off a game) and number picks the background wash.
  bool drawDeviceMenu(uint8_t sigilId, const char *who = nullptr, uint8_t number = 0);
  void menuRow(const char *label, const char *note, PadRect rect, uint8_t key, uint8_t action, uint32_t meaning);
  // The confirm popup over the screen: Cancel (Left) and the action (Select).
  void confirmPopup();
  void menuClose(const char *label, PadRect rect, uint32_t meaning);
  void statusScreen(const char *title, const char *line1, const char *line2, uint8_t sigilId,
      PadLabels labels);
  void finish(const PadLabels &labels);
  bool lifeRequestShown() const { return life_.request.target != 0; }
  bool lifeKeysShown() const;

  char seatName_[2][TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  lv_obj_t *keys_[KEY_COUNT] = {};
  Key pressed_ = Key::Count;
  enum class Layout : uint8_t { Actions, State, Game, Menu, Picker, Commander, Hidden };
  Layout layout_ = Layout::Actions;
  TouchMap touchMap_;
  TurnHubUi::Page page_ = TurnHubUi::Page::Game;
  // The player and life request the game screen shows. Every frame stamps
  // them on its touch map, so the open Menu over a game accepts touches too.
  uint8_t actor_ = 0;
  uint16_t lifeRequestKey_ = 0;
  uint32_t context_ = 0;
};

}  // namespace TurnHubSigil
